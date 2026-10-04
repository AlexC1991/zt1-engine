#define SDL_MAIN_HANDLED

#include <SDL2/SDL.h>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <vector>
#include <string>
#include <thread>
#include <unordered_map>

#include "Animation.hpp"
#include "ArtScaler.hpp"
#include "GpuFsr.hpp"
#include "ItemCatalog.hpp"
#include "CompassDirection.hpp"
#include "Config.hpp"
#include "RenderSettings.hpp"
#include "IniReader.hpp"
#include "Input.hpp"
#include "InputManager.hpp"
#include "LoadScreen.hpp"
#include "Research.hpp"
#include "ResourceManager.hpp"
#include "ScenarioManager.hpp"

#include "Utils.hpp"
#include "Window.hpp"
#include "World.hpp"
#include "DevConsole.hpp"
#include "Features.hpp"
#include "Pathfinder.hpp"
#include <random>
#include <sstream>
#include <queue>
#include "ZooSim.hpp"

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
// Freeform items month by month (the original), or all from the start
static bool g_timedUnlocks = false;
// The month the catalogue lists items for: every freeform unlock goal's
// month has passed when they're all unlocked
static int unlockMonthFor(int month) { return Features::allUnlocked ? 1000 : month; }
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
static ZooSim g_sim; // the game clock and the zoo's books
static DevConsole g_console;    // ` or F10: commands to set up test states
static float g_gameSpeed = 1.0f; // (dev console) the game's speed

static std::string formatMoney(int amount) {
  std::string digits = std::to_string(amount < 0 ? -amount : amount);
  for (int i = static_cast<int>(digits.size()) - 3; i > 0; i -= 3)
    digits.insert(i, ",");
  return (amount < 0 ? "-$" : "$") + digits;
}

// The HUD's date ("Apr, Year 1": lang string 22012's MMM',' 'Year' y) and
// money (green, red when below zero)
static void updateHudClock(ResourceManager *rm) {
  if (!g_hud)
    return;
  if (UiText *date = dynamic_cast<UiText *>(g_hud->getElementById(1030)))
    date->setText(rm->getString(22101 + g_sim.month()) + ", Year " +
                  std::to_string(g_sim.year()));
  if (UiText *money = dynamic_cast<UiText *>(g_hud->getElementById(1016))) {
    long cash = std::lround(std::floor(g_sim.cash()));
    money->setText(formatMoney(static_cast<int>(cash)));
    money->setTextColor(cash >= 0 ? SDL_Color{83, 219, 83, 255}
                                  : SDL_Color{255, 60, 60, 255});
  }
  if (auto *zoo = dynamic_cast<UiStatusImage *>(g_hud->getElementById(1015)))
    zoo->setValue(static_cast<int>(g_sim.rating()));
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

// The in-game screen for the next game, built when the freeform menu opens
// so that Play goes straight into the game, as in the original (building it
// takes a moment: every panel's layout and art)
static UiGameScreen *g_nextHud = nullptr;
static void prepareHud(ResourceManager *rm) {
  if (!g_nextHud)
    g_nextHud = new UiGameScreen(rm, "ui/main.lyt", "ui/gamescrn.lyt");
}

static void createHud(ResourceManager *rm, World *world, int startingCash) {
  destroyHud();
  // The HUD and its panels, as the original's in-game screen
  g_hud = g_nextHud ? g_nextHud
                    : new UiGameScreen(rm, "ui/main.lyt", "ui/gamescrn.lyt");
  g_nextHud = nullptr;

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
  // A new zoo on Death Mountain rates 37 in the original (its Zoo Status
  // rating graph)
  const int rating = 37;
  if (auto *zoo = dynamic_cast<UiStatusImage *>(g_hud->getElementById(1015)))
    zoo->setValue(rating);

  // The Zoo Status panel: the zoo's name (its entrance's), admission (every
  // shipped map has $22.00), cash and the value of what is on the map
  UiGameScreen::ZooInfo info;
  info.name = world->getMap().getZooName();
  info.cash = startingCash;
  info.rating = rating;
  info.month = "Jan";
  ItemCatalog::get().load(rm);
  std::unordered_map<std::string, int> costByType;
  for (const CatalogItem &item : ItemCatalog::get().all()) {
    size_t slash = item.file.find_last_of('/'), dot = item.file.find_last_of('.');
    costByType.emplace(item.file.substr(slash + 1, dot - slash - 1), item.cost);
  }
  for (const WorldMap::Placed &p : world->getMap().getPlaced()) {
    auto it = costByType.find(p.typeName); // fences go by their subclass
    if (it == costByType.end())
      it = costByType.find(p.subClass);
    if (it != costByType.end())
      info.placedValue += it->second;
  }
  g_hud->setZooInfo(info);

  // A new game starts on January of year 1 with the starting cash
  g_sim.start(startingCash, rating);
  ItemCatalog::get().setMonth(unlockMonthFor(1));
  g_hud->refreshCatalog();
  g_hud->setSim(&g_sim);
  g_hud->setFences(&world->getFences(), [world](float x, float y) {
    world->centreOn(x, y);
  });
  // Staff Information's buttons
  g_hud->setStaff(&world->getStaff(), [world](UiGameScreen::StaffRequest r, int id) {
    switch (r) {
    case UiGameScreen::StaffRequest::PickUp: world->pickUpStaff(id); break;
    case UiGameScreen::StaffRequest::Fire: world->getStaff().fire(id); break;
    case UiGameScreen::StaffRequest::Assign: world->assignStaff(id); break;
    case UiGameScreen::StaffRequest::Track: world->trackStaff(id); break;
    case UiGameScreen::StaffRequest::Select: world->centreOnStaff(id); break;
    }
  });
  updateHudClock(rm);
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
    if (g_hud->hasPopup())
      drawUiScaled(renderer, rm, t,
                   [&](SDL_Renderer *r) { g_hud->drawPopup(r, &rect); });
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
  // A drop-down list goes over everything, text included
  if (g_hud->hasPopup())
    drawUiScaled(renderer, rm, t, [&](SDL_Renderer *r) {
      SDL_Rect viewport = {static_cast<int>(std::lround(t.offsetX / t.scale)),
                           static_cast<int>(std::lround(t.offsetY / t.scale)),
                           UI_LAYOUT_W, UI_LAYOUT_H};
      SDL_RenderSetViewport(r, &viewport);
      SDL_Rect layoutRect = {0, 0, UI_LAYOUT_W, UI_LAYOUT_H};
      g_hud->drawPopup(r, &layoutRect);
      SDL_RenderSetViewport(r, nullptr);
    });
  // A dialog (new exhibit name, are you sure) over everything
  if (g_hud->hasDialog())
    drawUiScaled(renderer, rm, t, [&](SDL_Renderer *r) {
      SDL_Rect viewport = {static_cast<int>(std::lround(t.offsetX / t.scale)),
                           static_cast<int>(std::lround(t.offsetY / t.scale)),
                           UI_LAYOUT_W, UI_LAYOUT_H};
      SDL_RenderSetViewport(r, &viewport);
      SDL_Rect layoutRect = {0, 0, UI_LAYOUT_W, UI_LAYOUT_H};
      g_hud->drawDialog(r, &layoutRect);
      SDL_RenderSetViewport(r, nullptr);
    });
}

// ----------------------------------------------------------------------------
// The map: fences laid, exhibits named, tank walls bulldozed
// ----------------------------------------------------------------------------
static int g_mouseX = 0, g_mouseY = 0;

// What a fence drag or bulldozer click did: charge it, name new exhibits,
// ask before draining a tank
static void handleToolResult(const World::ToolResult &r, ResourceManager *rm) {
  if (r.cost > 0)
    g_sim.spend(ZooSim::Construction, r.cost);
  for (int id : r.newExhibits) {
    const Fences::Exhibit *ex = g_world->getFences().exhibit(id);
    if (!ex)
      continue;
    // "Exhibit 1", "Tank 2": the count of every exhibit made
    std::string initial = rm->getString(ex->tank ? 6209 : 6208) + " " + std::to_string(id);
    g_hud->askName(rm->getString(158), initial, [id](const std::string &name) {
      std::string n = name;
      if (n.empty())
        if (const Fences::Exhibit *e = g_world->getFences().exhibit(id))
          n = e->name;
      int gate = g_world->getFences().finishExhibit(id, n, g_world->getMap());
      if (gate > 0)
        g_sim.spend(ZooSim::Construction, gate);
    });
  }
  if (r.askDrainTank)
    g_hud->askConfirm(rm->getString(160), [rm] {
      if (int refund = g_world->confirmDrain())
        g_sim.earn(ZooSim::Recycling, refund);
      updateHudClock(rm);
    });
  if (r.exhibit >= 0)
    g_hud->showExhibit(r.exhibit);
  if (r.outsideZoo)
    g_hud->showMessage(rm->getString(10311)); // "...inside the main zoo wall."
  if (r.messageId)
    g_hud->showMessage(rm->getString(r.messageId));
  if (r.filter >= 0)
    g_hud->showFilter(r.filter);
  // Bulldozed: money back, as Recycling Benefit
  if (r.refund > 0)
    g_sim.earn(ZooSim::Recycling, r.refund);
  // Hired: the first month's pay at once
  if (r.wage > 0)
    g_sim.spend(ZooSim::Wages, r.wage);
  if (r.staff >= 0 && r.wage == 0)
    g_hud->showStaff(r.staff);
  if (r.wage > 0 || r.staff >= 0) {
    UiGameScreen::ZooInfo info = g_hud->zooInfo();
    info.staffCount = static_cast<int>(g_world->getStaff().members().size());
    g_hud->setZooInfo(info);
  }
  // The money shown changes at once (as the original's)
  updateHudClock(rm);
}

static void worldInputs(SDL_Renderer *renderer, ResourceManager *rm,
                        const std::vector<Input> &raw) {
  if (!g_hud || !g_world)
    return;
  UiTransform t = getUiTransform(renderer);
  if (g_widescreen)
    t.offsetX = t.offsetY = 0;
  // The tool picked: a fence in Buy Habitat, or the bulldozer
  int tool = -1;
  std::string file = g_hud->fenceToolFile();
  if (!file.empty()) {
    const auto &types = g_world->getFences().types();
    for (size_t i = 0; i < types.size(); i++)
      if (types[i].file == file)
        tool = static_cast<int>(i);
  }
  if (g_hud->bulldozerOn())
    tool = -1; // the bulldozer, while it's picked
  if (tool != g_world->getFenceTool())
    g_world->setFenceTool(tool);
  // Over the map, the fence tool's hint ("Click to place a single fence
  // piece, or click and drag to create multiple fence pieces.")
  g_hud->setWorldTip(!Features::mapTooltips              ? std::string()
                     : g_world->getFenceTool() >= 0     ? rm->getString(31077)
                     : !g_world->getPathTool().empty() ? rm->getString(31078)
                                                       : "");
  // A path picked on the Paths tab: laid tile by tile
  {
    std::string file = g_hud->bulldozerOn() ? "" : g_hud->pathToolFile();
    std::string key;
    int cost = 0;
    if (!file.empty()) {
      size_t slash = file.find_last_of('/'), dot = file.find_last_of('.');
      key = file.substr(slash + 1, dot - slash - 1);
      for (const CatalogItem &item : ItemCatalog::get().all())
        if (item.file == file)
          cost = item.cost;
    }
    g_world->setPathTool(key, cost);
  }
  // Staff picked in Hire Staff are put down like objects
  {
    std::string staffFile = g_hud->staffToolFile();
    int staffType = staffFile.empty() || g_hud->bulldozerOn()
                        ? -1
                        : g_world->getStaff().typeOfFile(staffFile);
    g_world->setStaffTool(staffType);
    // Over a staff member: its name
    const Staff &st = g_world->getStaff();
    if (st.hovered >= 0 && g_world->getFenceTool() < 0)
      if (const Staff::Member *m = st.member(st.hovered))
        g_hud->setWorldTip(m->name);
  }
  // The bulldozer over a piece: its name
  if (g_world->isBulldozing() && !g_world->bulldozeName().empty())
    g_hud->setWorldTip(g_world->bulldozeName());
  // The tank filter, in the fence list, is placed like an object
  g_world->setFilterTool(!g_hud->bulldozerOn() && !file.empty() &&
                         file == g_world->getFences().filterType().file);
  g_world->setBulldozer(g_hud->bulldozerOn());
  g_world->setDate(g_sim.dayOfMonth(), g_sim.month(), g_sim.year());

  for (const Input &in : raw) {
    if (in.type != InputType::POSITIONED)
      continue;
    int lx = static_cast<int>((in.x - t.offsetX) / t.scale);
    int ly = static_cast<int>((in.y - t.offsetY) / t.scale);
    bool overUi = g_hud->isOverHud(lx, ly);
    if (in.event == InputEvent::CURSOR_MOVE) {
      g_mouseX = in.x;
      g_mouseY = in.y;
      if (overUi)
        g_world->hoverOff();
      else
        g_world->mouseMove(in.x, in.y);
    } else if (in.event == InputEvent::LEFT_CLICK && !overUi) {
      handleToolResult(g_world->mouseDown(in.x, in.y), rm);
    } else if (in.event == InputEvent::LEFT_RELEASE) {
      handleToolResult(g_world->mouseUp(in.x, in.y), rm);
    } else if (in.event == InputEvent::RIGHT_CLICK) {
      g_world->cancelTool();
    }
  }
}

// ----------------------------------------------------------------------------
// Dev console commands: put the zoo into states to watch the staff and tanks
// deal with (fake animal counts, dung, litter, worn fences, dirty water)
// ----------------------------------------------------------------------------
static std::string devCommand(const std::vector<std::string> &w, ResourceManager *rm) {
  if (!g_world)
    return "No game running.";
  Fences &fences = g_world->getFences();
  Staff &staff = g_world->getStaff();
  ZooItems &items = g_world->getItems();
  WorldMap &map = g_world->getMap();
  static std::mt19937 rng(42);
  auto num = [&](size_t i, float fallback) {
    return i < w.size() ? static_cast<float>(std::atof(w[i].c_str())) : fallback;
  };
  // The named exhibits, as numbered in 'exhibits'
  std::vector<Fences::Exhibit *> named;
  for (const Fences::Exhibit &e : fences.exhibits())
    if (e.named)
      named.push_back(fences.exhibit(e.id));
  auto exhibitArg = [&](size_t i) -> Fences::Exhibit * {
    if (i >= w.size())
      return nullptr;
    int n = std::atoi(w[i].c_str());
    if (n >= 1 && n <= static_cast<int>(named.size()))
      return named[n - 1];
    for (Fences::Exhibit *e : named)
      if (Utils::string_to_lower(e->name).find(Utils::string_to_lower(w[i])) != std::string::npos)
        return e;
    return nullptr;
  };
  auto randomTile = [&](const Fences::Exhibit *e, float &x, float &y) {
    if (!e || e->tiles.empty())
      return false;
    auto it = e->tiles.begin();
    std::advance(it, std::uniform_int_distribution<size_t>(0, e->tiles.size() - 1)(rng));
    std::uniform_real_distribution<float> in(0.25f, 0.75f);
    x = it->first + in(rng);
    y = it->second + in(rng);
    return true;
  };
  const std::string &c = w[0];
  std::string out;
  if (c == "help") {
    return "exhibits                         list exhibits (their numbers for the rest)\n"
           "animals <ex> <n> [zoo|dino] [herb|carn]   fake animal count (tanks: marine)\n"
           "dung <ex> [n]                    drop dung piles in an exhibit\n"
           "food <ex> | nofood <ex>          put food down / take it away\n"
           "litter [n]                       scatter litter round the view\n"
           "wear <ex|all> [life] | break <ex|all>   wear fences down / break them\n"
           "filters [health]                 set every tank filter's health (0-10)\n"
           "dirty <ex> [purity]              a tank's water purity (default 30)\n"
           "staff | assign <staff#> <ex>     list staff / put one on an exhibit\n"
           "money <n> | day [n] | month [n] | speed <x>\n"
           "paths [on|off]                   path debug overlay (and routes to the log)\n"
           "features | set <name> on|off     our additions on/off (off = original)\n"
           "raise|lower <x> <y> [n]          a grid corner up/down (makes ramps)\n"
           "layroad <x0> <y0> <x1> <y1>      a test path, x first then y\n"
           "clear";
  }
  if (c == "exhibits") {
    if (named.empty())
      return "No exhibits yet.";
    char b[200];
    for (size_t i = 0; i < named.size(); i++) {
      const Fences::Exhibit *e = named[i];
      std::snprintf(b, sizeof b, "%zu. %-14s %s  animals %d%s  food %.0f  dung %zu%s", i + 1,
                    e->name.c_str(), e->tank ? "tank" : "land", e->testAnimals,
                    e->testDino ? " dino" : "", items.foodIn(e->id),
                    items.ofKind(ZooItems::Kind::Dung, e->id).size(),
                    e->tank ? ("  purity " + std::to_string(static_cast<int>(e->purity))).c_str() : "");
      out += std::string(b) + "\n";
    }
    return out;
  }
  if (c == "animals") {
    Fences::Exhibit *e = exhibitArg(1);
    if (!e)
      return "Which exhibit? (see 'exhibits')";
    e->testAnimals = std::max(0, static_cast<int>(num(2, 1)));
    for (size_t i = 3; i < w.size(); i++) {
      if (w[i] == "dino") e->testDino = true;
      if (w[i] == "zoo") e->testDino = false;
      if (w[i] == "carn") e->testCarnivore = true;
      if (w[i] == "herb") e->testCarnivore = false;
    }
    return e->name + ": " + std::to_string(e->testAnimals) + " fake " +
           (e->tank ? "marine" : e->testDino ? "dinosaur" : "zoo") + " animals (" +
           (e->testCarnivore ? "carnivores" : "herbivores") + ")";
  }
  if (c == "dung") {
    Fences::Exhibit *e = exhibitArg(1);
    if (!e || e->tank)
      return "Which land exhibit?";
    int n = static_cast<int>(num(2, 3));
    for (int i = 0; i < n; i++) {
      float x, y;
      if (randomTile(e, x, y))
        items.add(ZooItems::Kind::Dung, e->id, x, y);
    }
    return std::to_string(n) + " dung piles in " + e->name;
  }
  if (c == "food" || c == "nofood") {
    Fences::Exhibit *e = exhibitArg(1);
    if (!e)
      return "Which exhibit?";
    if (c == "nofood") {
      items.removeIn(e->id, ZooItems::Kind::Food);
      return e->name + ": no food";
    }
    float x, y;
    if (randomTile(e, x, y))
      items.addFood(e->id, x, y, std::max(1, e->testAnimals),
                    e->tank ? "fish" : e->testCarnivore ? "carnchow" : "herbchow");
    return e->name + ": food put down";
  }
  if (c == "litter") {
    float cx, cy;
    if (!g_world->viewCentre(cx, cy))
      return "Point the view at the zoo.";
    int n = static_cast<int>(num(1, 10)), put = 0;
    std::uniform_real_distribution<float> off(-8.0f, 8.0f);
    for (int tries = 0; tries < n * 20 && put < n; tries++) {
      float x = cx + off(rng), y = cy + off(rng);
      int tx = static_cast<int>(std::floor(x)), ty = static_cast<int>(std::floor(y));
      // Litter is dropped where guests walk: on the paths
      if (map.isPath(tx, ty) && fences.tileFit(tx, ty, map) == Fences::Fit::Ok &&
          fences.exhibitAt(tx, ty) < 0) {
        items.add(ZooItems::Kind::Litter, -1, x, y);
        put++;
      }
    }
    return std::to_string(put) + " pieces of litter dropped on the paths";
  }
  if (c == "wear" || c == "break") {
    bool all = w.size() > 1 && w[1] == "all";
    Fences::Exhibit *e = all ? nullptr : exhibitArg(1);
    if (!all && !e)
      return "Which exhibit, or 'all'?";
    int n = 0;
    std::vector<Fences::Edge> edges;
    for (const auto &kv : fences.pieces())
      if (!fences.types()[kv.second.type].indestructible && kv.second.tank < 0 &&
          (all || fences.exhibitOf(kv.first) == e->id))
        edges.push_back(kv.first);
    for (const Fences::Edge &edge : edges) {
      const FenceType &t = fences.types()[fences.at(edge)->type];
      float life = c == "break" ? 0.0f : num(2, static_cast<float>(t.decayedLife));
      fences.setLife(edge, life);
      n++;
    }
    return std::to_string(n) + " fence pieces " + (c == "break" ? "broken" : "worn");
  }
  if (c == "filters") {
    int n = 0;
    for (size_t i = 0; i < fences.filters().size(); i++)
      if (Fences::Filter *f = fences.filter(static_cast<int>(i))) {
        f->health = num(1, static_cast<float>(fences.filterType().decayedHealth));
        n++;
      }
    return std::to_string(n) + " filters set";
  }
  if (c == "dirty") {
    Fences::Exhibit *e = exhibitArg(1);
    if (!e || !e->tank)
      return "Which tank?";
    e->purity = std::clamp(num(2, 30), 0.0f, 100.0f);
    return e->name + ": water purity " + std::to_string(static_cast<int>(e->purity));
  }
  if (c == "staff") {
    if (staff.members().empty())
      return "No staff.";
    for (size_t i = 0; i < staff.members().size(); i++) {
      const Staff::Member &m = staff.members()[i];
      out += std::to_string(i + 1) + ". " + m.name + " - " + staff.dutyText(m) +
             (m.exhibits.empty() ? "" : "  (" + std::to_string(m.exhibits.size()) + " exhibits)") + "\n";
    }
    return out;
  }
  if (c == "assign") {
    int i = static_cast<int>(num(1, 0)) - 1;
    Fences::Exhibit *e = exhibitArg(2);
    if (i < 0 || i >= static_cast<int>(staff.members().size()) || !e)
      return "assign <staff#> <exhibit#>";
    int msg = staff.assign(staff.members()[i].id, e->id, fences);
    return msg ? rm->getString(msg) : staff.members()[i].name + " now looks after " + e->name;
  }
  if (c == "walk") {
    int i = static_cast<int>(num(1, 0)) - 1;
    if (i < 0 || i >= static_cast<int>(staff.members().size()) || w.size() < 4)
      return "walk <staff#> <x> <y>";
    bool ok = staff.walkTo(staff.members()[i].id, std::atoi(w[2].c_str()), std::atoi(w[3].c_str()), map, fences);
    return ok ? staff.members()[i].name + " walking there" : std::string("No way there.");
  }
  if (c == "money") {
    g_sim.setCash(num(1, 75000));
    updateHudClock(rm);
    return "Cash set.";
  }
  if (c == "day" || c == "month") {
    int days = static_cast<int>(num(1, 1)) * (c == "month" ? 30 : 1);
    const Uint8 none[SDL_NUM_SCANCODES] = {};
    for (int s = 0; s < days * 120; s++) {
      g_world->update(none, 0.1f);
      g_sim.update(0.1f);
    }
    updateHudClock(rm);
    return std::to_string(days) + " days passed.";
  }
  if (c == "features") {
    for (const Features::Switch &f : Features::all())
      out += std::string(*f.value ? "[on]  " : "[off] ") + f.name + " - " + f.about + "\n";
    return out + "set <name> on|off   (off = the original game's way)";
  }
  if (c == "set") {
    if (w.size() < 3)
      return "set <name> on|off";
    for (Features::Switch &f : Features::all())
      if (w[1] == f.name) {
        *f.value = w[2] == "on" || w[2] == "1" || w[2] == "true";
        if (std::string(f.name) == "fencemodes")
          g_world->cancelTool();
        if (std::string(f.name) == "allunlocked") {
          ItemCatalog::get().setMonth(unlockMonthFor(std::max(1, static_cast<int>(g_sim.history().size()))));
          if (g_hud)
            g_hud->refreshCatalog();
        }
        return std::string(f.name) + (*f.value ? " on" : " off (original)");
      }
    return "No switch '" + w[1] + "' - see 'features'.";
  }
  if (c == "ramp" || c == "raise" || c == "lower") {
    // A grid corner (vertex) raised or lowered a unit, the tiles round it
    // with it: ground stays joined (a ramp, not a cliff)
    if (w.size() < 3)
      return c + " <x> <y> [units]: a grid corner up (raise/ramp) or down (lower)";
    int vx = std::atoi(w[1].c_str()), vy = std::atoi(w[2].c_str());
    int by = std::max(1, static_cast<int>(num(3, 1)));
    map.raiseVertex(vx, vy, c == "lower" ? -by : by);
    g_world->reindex();
    return "Corner " + w[1] + "," + w[2] + (c == "lower" ? " lowered" : " raised");
  }
  if (c == "layroad") {
    // A straight path from one tile to another (for tests)
    if (w.size() < 5)
      return "layroad <x0> <y0> <x1> <y1> [type]";
    int x0 = std::atoi(w[1].c_str()), y0 = std::atoi(w[2].c_str());
    int x1 = std::atoi(w[3].c_str()), y1 = std::atoi(w[4].c_str());
    std::string type = w.size() > 5 ? w[5] : "path";
    int n = 0;
    for (int x = x0, y = y0;; ) {
      if (map.pathShapeOk(x, y)) {
        map.setPath(x, y, type);
        n++;
      }
      if (x == x1 && y == y1)
        break;
      if (x != x1)
        x += x1 > x ? 1 : -1;
      else
        y += y1 > y ? 1 : -1;
    }
    return std::to_string(n) + " path tiles laid";
  }
  if (c == "paths") {
    // The path debug overlay, and every route planned to the log
    g_world->debugPaths = w.size() > 1 ? w[1] != "off" : !g_world->debugPaths;
    staff.logRoutes = g_world->debugPaths;
    return std::string("Path debug ") + (g_world->debugPaths ? "on: blocked tiles red, routes lit, goals outlined" : "off");
  }
  if (c == "speed") {
    g_gameSpeed = std::clamp(num(1, 1), 0.1f, 20.0f);
    char b[64];
    std::snprintf(b, sizeof b, "Game speed x%.1f", g_gameSpeed);
    return b;
  }
  return "Unknown command '" + c + "' - try 'help'.";
}

// The fence's price beside the cursor while laying it
static void drawToolCost(SDL_Renderer *renderer, ResourceManager *rm) {
  if (!g_world || (g_world->getFenceTool() < 0 && !g_world->getFilterTool() &&
                   g_world->getStaffTool() < 0 && !g_world->isBulldozing() &&
                   g_world->getPathTool().empty()))
    return;
  UiTransform t = getUiTransform(renderer);
  rm->setTextScale(t.scale);
  // The drag mode (or a path's build height), under where the price goes
  bool pathHeight = !g_world->getPathTool().empty() && Features::elevatedPaths;
  bool canRaise = World::raisable(g_world->getPathTool());
  if ((g_world->getFenceTool() >= 0 && Features::fenceModes) || pathHeight) {
    std::string mode = pathHeight && !canRaise
                           ? std::string("Ground only")
                           : pathHeight
                           ? (g_world->getBuildHeight() > 0
                                  ? "Height +" + std::to_string(g_world->getBuildHeight()) + " (PgUp/PgDn)"
                                  : std::string("Ground (PgUp: raise)"))
                           : std::string("Tab: ") + World::fenceModeName(g_world->getFenceMode());
    if (SDL_Texture *m = rm->getStringTexture(renderer, 4136, mode,
                                              SDL_Color{230, 230, 230, 255}, 4137)) {
      int w = 0, h = 0;
      rm->getTextSize(m, &w, &h);
      SDL_Rect r = {g_mouseX + static_cast<int>(14 * t.scale),
                    g_mouseY + static_cast<int>(10 * t.scale), w, h};
      SDL_RenderCopy(renderer, m, nullptr, &r);
    }
  }
  if (g_world->hoverCost() < 0)
    return;
  // As the original: "$70" over a piece, "-$630" dragging (money going
  // out), "$0" where none can go
  std::string label = ((g_world->isDraggingFence() || g_world->isDraggingPath() || g_world->getStaffTool() >= 0) &&
                          g_world->hoverCost() > 0
                      ? "-"
                      : "") +
                      formatMoney(g_world->hoverCost());
  SDL_Texture *text = rm->getStringTexture(renderer, 4136, label,
                                           SDL_Color{255, 223, 41, 255}, 4137);
  if (!text)
    return;
  int w = 0, h = 0;
  rm->getTextSize(text, &w, &h);
  SDL_Rect r = {g_mouseX + static_cast<int>(14 * t.scale), g_mouseY - h / 2, w, h};
  SDL_RenderCopy(renderer, text, nullptr, &r);
}

extern float g_ZoomLevel; // World.cpp

static UiAction hudInputs(SDL_Renderer *renderer, std::vector<Input> inputs) {
  if (!g_hud)
    return UiAction::NONE;
  UiTransform t = getUiTransform(renderer);
  if (g_widescreen)
    t.offsetX = t.offsetY = 0; // the HUD covers the whole window
  mapInputsToLayout(inputs, t);
  UiAction action = g_hud->handleInputs(inputs);
  // The wheel scrolls an open panel's lists (the panel handles that) and
  // zooms the map anywhere else
  for (const Input &in : inputs) {
    if (in.event != InputEvent::SCROLL_UP && in.event != InputEvent::SCROLL_DOWN)
      continue;
    if (g_hud->isOverPanel(in.x, in.y))
      continue;
    g_ZoomLevel += in.event == InputEvent::SCROLL_UP ? 0.1f : -0.1f;
    g_ZoomLevel = std::clamp(g_ZoomLevel, 0.1f, 3.0f);
  }
  return action;
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
  // As if from the freeform menu: the background preload has finished and
  // the in-game screen is ready
  ItemCatalog::get().load(rm);
  Research::get().load(rm);
  prepareHud(rm);
  Uint64 t0 = SDL_GetPerformanceCounter();
  auto ms = [&] {
    return (SDL_GetPerformanceCounter() - t0) * 1000.0 / SDL_GetPerformanceFrequency();
  };
  const FreeformMap *chosen = nullptr;
  for (int i = 0; const FreeformMap *map = sm->getFreeformMap(i); i++)
    if (getFileStem(map->path) == stem)
      chosen = map;
  if (!chosen || !world->loadFreeform(chosen->path)) {
    SDL_Log("[PanelShots] map %s not found", stem.c_str());
    return 1;
  }
  // Timings also go to <outDir>/timings.txt (a real console isn't captured)
  std::filesystem::create_directories(outDir);
  FILE *timings = fopen((outDir + "/timings.txt").c_str(), "w");
  auto note = [&](const char *what) {
    SDL_Log("[PanelShots] %s at %.0f ms", what, ms());
    if (timings)
      fprintf(timings, "%s %.0f ms\n", what, ms());
  };
  note("map loaded");
  createHud(rm, world, CASH_START);
  note("screen built");
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
      {3, "!open", "filterlist"},  {3, "!1", "filterzt"},
      {3, "!2", "filterdino"},
      {3, "!3", "filtermarine"},   {3, "!0", "filterall"},
      {15, "#4008", "research"},   {15, "#4009", "research2"},
      {15, "#4010", "conservation"}, {5, "", "gameopts"},
      {0, "~esc", "escmenu"},
      {14, "#4104", "zoo_info"},   {14, "#4154", "zoo_finance"},
      {14, "#4153", "zoo_graphs"}, {14, "#4105", "zoo_awards"},
      {14, "#4180", "zoo_commerce"}, {14, "#4181", "zoo_research"},
      {14, "#4153", "zoo_graphs"},   {14, "#13982", "zoo_donations"},
      {14, "#13983", "zoo_profit"},  {14, "#13984", "zoo_attendance"},
      {14, "#4106", "zoo_rating"},   {14, "#4152", "zoo_rating_bar"}};
  // Draws a few frames (art loads and settles) and saves the last
  auto capture = [&](const char *name) {
    for (int frame = 0; frame < 3; frame++) {
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
      SDL_SaveBMP(s, (outDir + "/" + name + ".bmp").c_str());
    if (s)
      SDL_FreeSurface(s);
    SDL_RenderPresent(renderer);
    note(name);
  };
  for (const Shot &shot : shots) {
    // "!open": the panel's content filter list dropped down; "!<n>": a
    // filter choice, on the animals tab
    // "~esc": the game menu as ESC opens it
    if (shot.tab[0] == '~') {
      g_hud->showPanel(0, "");
      g_hud->toggleGameMenu();
    } else if (shot.tab[0] == '!') {
      g_hud->showPanel(shot.panel, "animals");
      if (std::string(shot.tab) == "!open")
        g_hud->openFilter(shot.panel);
      else
        g_hud->setFilter(shot.panel, std::atoi(shot.tab + 1));
    } else if (shot.tab[0] == '#') {
      g_hud->showPanel(shot.panel, "");
      g_hud->showTab(shot.panel, std::atoi(shot.tab + 1));
    } else {
      g_hud->showPanel(shot.panel, shot.tab);
    }
    capture(shot.name);
    // ZT_START_ONLY=1: just the start view (for calibrating it)
    if (std::getenv("ZT_START_ONLY")) {
      int w = 0, h = 0;
      SDL_GetRendererOutputSize(renderer, &w, &h);
      float sc = h / 600.0f, ox = (w - 800 * sc) / 2;
      for (const PlacedObjects::Object &o : world->getObjects().objects()) {
        const std::string &t = o.typeName;
        if (o.className != "building" || t.size() < 4 || t.compare(t.size() - 4, 4, "gate") != 0)
          continue;
        int px, py;
        world->vertexToWindow(static_cast<int>(std::lround(o.x)), static_cast<int>(std::lround(o.y)), px, py);
        SDL_Log("[PanelShots] entrance %s at %.1f,%.1f screen800 %.0f,%.0f", t.c_str(), o.x, o.y,
                (px - ox) / sc, py / sc);
      }
      return 0;
    }
  }

  // ZT_CATALOG_PAGES=1: every buy tab's grid, page by page (5 rows a page,
  // the last page scrolled to the end, as the original's arrows go)
  if (std::getenv("ZT_CATALOG_PAGES")) {
    const struct {
      int panel;
      const char *cat;
    } tabs[] = {{3, "animals"}, {3, "shelters"}, {3, "toys"}, {3, "showtoys"},
                {4, "structures"}, {4, "scenery"}, {8, "fence"}, {8, "paths"},
                {8, "foliage"}, {8, "rocks"}, {10, ""}};
    for (const auto &t : tabs) {
      g_hud->showPanel(t.panel, t.cat);
      int last = g_hud->scrollBuyPanel(t.panel, 0);
      for (int page = 0;; page++) {
        int row = std::min(page * 5, last);
        g_hud->scrollBuyPanel(t.panel, row);
        std::string name = std::string("cp_") + (t.cat[0] ? t.cat : "staff") + "_" +
                           (page < 10 ? "0" : "") + std::to_string(page);
        capture(name.c_str());
        if (row >= last)
          break;
      }
      g_hud->scrollBuyPanel(t.panel, 0);
    }
    return 0;
  }

  // Buttons clicked with made-up input (layout units, on the button as
  // last drawn), returning the action it asked for
  auto click = [&](int id) {
    UiElement *e = g_hud->getElementById(id);
    if (!e)
      return UiAction::NONE;
    SDL_Rect r = e->getLastRect();
    Input in{};
    in.type = InputType::POSITIONED;
    in.event = InputEvent::LEFT_CLICK;
    in.position = {r.x + r.w / 2, r.y + r.h / 2};
    in.x = in.position.x;
    in.y = in.position.y;
    std::vector<Input> inputs = {in};
    g_hud->handleInputs(inputs);
    std::vector<Input> none;
    return g_hud->handleInputs(none); // the action it asked for
  };
  auto check = [&](const char *what, bool ok) {
    std::string line = std::string(what) + (ok ? " ok" : " FAILED");
    note(line.c_str());
  };

  // The rotate buttons turn the picked shelter (right twice, left once)
  g_hud->showPanel(3, "shelters");
  capture("rotate0");
  click(2055);
  capture("rotate1");
  click(2055);
  capture("rotate2");
  click(2054);
  capture("rotate3");

  // The habitat panel's tabs clicked down to terraform and back up: each
  // shows its own page again
  {
    g_hud->showPanel(8, "fence");
    const int down[] = {3251, 3056, 3252, 3256, 3362, 3361, 3256, 3252, 3056, 3251};
    for (int i = 0; i < 10; i++) {
      capture(("bt_" + std::to_string(i) + "_" + std::to_string(down[i])).c_str());
      click(down[i]);
    }
    capture("bt_back_fence");
    UiElement *grid = g_hud->getElementById(3208);
    UiLayout *page = dynamic_cast<UiLayout *>(g_hud->getElementById(7));
    check("back on fences after terraform: the grid, not terraform",
          grid && !grid->isHidden() && page && page->isHidden());
    UiElement *fenceType = g_hud->getElementById(3299), *rotate = g_hud->getElementById(3258);
    check("fences show their type, no rotate arrows",
          fenceType && !fenceType->isHidden() && (!rotate || rotate->isHidden()));
    g_hud->showPanel(0, "");
  }

  // Zoo Status: marketing + and the admission arrows
  auto textOf = [&](int id) {
    UiText *t = dynamic_cast<UiText *>(g_hud->getElementById(id));
    return t ? t->getText() : std::string("?");
  };
  g_hud->showPanel(14, "");
  g_hud->showTab(14, 4104);
  capture("zoo_info2");
  click(4053);
  check("marketing + gives $200 min", textOf(4054) == "$200 min" &&
                                          !g_hud->getElementById(4052)->isDisabled());
  click(4195);
  check("admission up gives $22.25 / $11.13",
        textOf(4193) == "$22.25" && textOf(4194) == "$11.13");
  click(4191);
  click(4052);
  check("and back", textOf(4193) == "$22.00" && textOf(4054) == "$0 none");

  // Typing: the zoo's name (Backspace x3, "Park", Enter) and the price
  auto key = [&](InputEvent event, int code, const char *typed) {
    Input in{};
    in.type = InputType::BUTTON;
    in.event = event;
    in.key = code;
    SDL_strlcpy(in.text, typed, sizeof(in.text));
    std::vector<Input> inputs = {in};
    g_hud->handleInputs(inputs);
  };
  click(4199);
  for (int i = 0; i < 3; i++)
    key(InputEvent::KEY_DOWN, SDLK_BACKSPACE, "");
  key(InputEvent::TEXT_INPUT, 0, "Park");
  key(InputEvent::KEY_DOWN, SDLK_RETURN, "");
  g_hud->setZooInfo(g_hud->zooInfo());
  check("typed zoo name", textOf(4199) == "Death Mountain Park");
  click(4190);
  for (int i = 0; i < 5; i++)
    key(InputEvent::KEY_DOWN, SDLK_BACKSPACE, "");
  key(InputEvent::TEXT_INPUT, 0, "30.1");
  key(InputEvent::KEY_DOWN, SDLK_RETURN, "");
  g_hud->setZooInfo(g_hud->zooInfo());
  check("typed admission $30.00 / $15.00",
        textOf(4193) == "$30.00" && textOf(4194) == "$15.00");

  // Fences, as done in the original: a chain-link loop round 3 x 3 tiles,
  // named; an Atlantean tank wall loop round 2 x 2, filled; then one tank
  // wall bulldozed (after the drain question)
  int testSpotX = 16, testSpotY = 38; // (staff are hired near the test exhibit)
  {
    g_hud->showPanel(0, "");
    Fences &fences = world->getFences();
    auto lay = [&](const char *key, std::vector<std::pair<int, int>> loop) {
      world->setFenceTool(fences.typeIndex(key));
      for (size_t i = 0; i + 1 < loop.size(); i++) {
        int x0, y0, x1, y1;
        world->vertexToWindow(loop[i].first, loop[i].second, x0, y0);
        world->vertexToWindow(loop[i + 1].first, loop[i + 1].second, x1, y1);
        handleToolResult(world->mouseDown(x0, y0), rm);
        world->mouseMove(x1, y1);
        handleToolResult(world->mouseUp(x1, y1), rm);
      }
      world->setFenceTool(-1);
    };
    auto enter = [&] {
      Input in{};
      in.type = InputType::BUTTON;
      in.event = InputEvent::KEY_DOWN;
      in.key = SDLK_RETURN;
      std::vector<Input> inputs = {in};
      g_hud->handleInputs(inputs);
    };
    // Clear ground near the entrance: no fences in or round an n x n
    // block, no water, the same pocket of the zoo
    // (and reachable on foot from the entrance: not up a cliff)
    std::vector<char> reachable;
    {
      WorldMap &map = world->getMap();
      const int W = map.getWidth(), H = map.getHeight();
      reachable.assign(static_cast<size_t>(W) * H, 0);
      auto edgeOk = [&](int x, int y, int nx, int ny) {
        const MapTile *a = map.getTile(x, y), *b = map.getTile(nx, ny);
        if (!a || !b || world->getFences().tileFit(nx, ny, map) != Fences::Fit::Ok)
          return false;
        int a0, a1, b0, b1;
        if (nx > x) { a0 = a->cornerHeight[CORNER_X1Y0]; a1 = a->cornerHeight[CORNER_X1Y1]; b0 = b->cornerHeight[CORNER_X0Y0]; b1 = b->cornerHeight[CORNER_X0Y1]; }
        else if (nx < x) { a0 = a->cornerHeight[CORNER_X0Y0]; a1 = a->cornerHeight[CORNER_X0Y1]; b0 = b->cornerHeight[CORNER_X1Y0]; b1 = b->cornerHeight[CORNER_X1Y1]; }
        else if (ny > y) { a0 = a->cornerHeight[CORNER_X0Y1]; a1 = a->cornerHeight[CORNER_X1Y1]; b0 = b->cornerHeight[CORNER_X0Y0]; b1 = b->cornerHeight[CORNER_X1Y0]; }
        else { a0 = a->cornerHeight[CORNER_X0Y0]; a1 = a->cornerHeight[CORNER_X1Y0]; b0 = b->cornerHeight[CORNER_X0Y1]; b1 = b->cornerHeight[CORNER_X1Y1]; }
        return a0 == b0 && a1 == b1;
      };
      // From the zoo ground nearest the entrance
      int sx = -1, sy = -1;
      for (int r = 0; r < 10 && sx < 0; r++)
        for (int dy = -r; dy <= r && sx < 0; dy++)
          for (int dx = -r; dx <= r && sx < 0; dx++)
            if (world->getFences().tileFit(16 + dx, 40 + dy, map) == Fences::Fit::Ok) {
              sx = 16 + dx;
              sy = 40 + dy;
            }
      std::queue<std::pair<int, int>> q;
      if (sx >= 0) {
        q.push({sx, sy});
        reachable[sy * W + sx] = 1;
      }
      const int d4[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
      while (!q.empty()) {
        auto [x, y] = q.front();
        q.pop();
        for (auto &dd : d4) {
          int nx = x + dd[0], ny = y + dd[1];
          if (nx < 0 || ny < 0 || nx >= W || ny >= H || reachable[ny * W + nx] || !edgeOk(x, y, nx, ny))
            continue;
          reachable[ny * W + nx] = 1;
          q.push({nx, ny});
        }
      }
    }
    {
      size_t nreach = 0;
      for (char c : reachable) nreach += c;
      note(("reachable from the entrance: " + std::to_string(nreach) + " tiles").c_str());
    }
    auto clearSpot = [&](int n, int avoidX, int avoidY) {
      WorldMap &map = world->getMap();
      for (int r = 2; r < 30; r++)
        for (int oy = -r; oy <= r; oy++)
          for (int ox = -r; ox <= r; ox++) {
            int x0 = 14 + ox, y0 = 40 + oy;
            if (std::abs(x0 - avoidX) < n + 3 && std::abs(y0 - avoidY) < n + 3)
              continue;
            bool ok = true;
            const MapTile *first = map.getTile(x0, y0);
            for (int y = y0 - 1; y <= y0 + n && ok; y++)
              for (int x = x0 - 1; x <= x0 + n && ok; x++) {
                const MapTile *t = map.getTile(x, y);
                if (!t || t->terrainType == 9 || t->terrainType == 10)
                  ok = false;
                if (ok && !reachable[static_cast<size_t>(y) * map.getWidth() + x])
                  ok = false;
                // No cliffs in or round it: each tile joins the ones to
                // its right and below
                (void)first;
                if (ok && x < x0 + n)
                  if (const MapTile *r = map.getTile(x + 1, y))
                    ok = t->cornerHeight[CORNER_X1Y0] == r->cornerHeight[CORNER_X0Y0] &&
                         t->cornerHeight[CORNER_X1Y1] == r->cornerHeight[CORNER_X0Y1];
                if (ok && y < y0 + n)
                  if (const MapTile *b = map.getTile(x, y + 1))
                    ok = t->cornerHeight[CORNER_X0Y1] == b->cornerHeight[CORNER_X0Y0] &&
                         t->cornerHeight[CORNER_X1Y1] == b->cornerHeight[CORNER_X1Y0];
                for (int e = 0; e < 2 && ok; e++)
                  if (fences.at({e == 0, x, y}) || fences.at({e == 0, x + 1, y + 1}))
                    ok = false;
              }
            // and no rocks or trees within a few tiles (big ones overhang)
            for (const PlacedObjects::Object &o : world->getObjects().objects())
              if (o.x > x0 - 3 && o.x < x0 + n + 3 && o.y > y0 - 3 && o.y < y0 + n + 3)
                ok = false;
            if (ok)
              return std::make_pair(x0, y0);
          }
      return std::make_pair(-1, -1);
    };
    auto [cx, cy] = clearSpot(3, -100, -100);
    testSpotX = cx + 1;
    testSpotY = cy - 2;
    note(("chain-link spot " + std::to_string(cx) + "," + std::to_string(cy)).c_str());
    double before = g_sim.cash();
    lay("chainlnk", {{cx, cy}, {cx + 3, cy}, {cx + 3, cy + 3}, {cx, cy + 3}, {cx, cy}});
    capture("fence_name_dialog");
    enter();
    note(("fence loop cost $" + std::to_string(std::lround(before - g_sim.cash()))).c_str());
    capture("fence_exhibit");
    before = g_sim.cash();
    auto [tx, ty] = clearSpot(2, cx, cy);
    note(("tank spot " + std::to_string(tx) + "," + std::to_string(ty)).c_str());
    lay("atltank", {{tx, ty}, {tx + 2, ty}, {tx + 2, ty + 2}, {tx, ty + 2}, {tx, ty}});
    enter();
    world->getFences().update(10.0f);
    note(("tank cost $" + std::to_string(std::lround(before - g_sim.cash()))).c_str());
    capture("fence_tank");
    g_hud->showPanel(30, "");
    if (UiListBox *list = dynamic_cast<UiListBox *>(g_hud->getElementById(4305))) {
      g_hud->refreshExhibits();
      list->setSelectedIndex(1);
      g_hud->refreshExhibits();
    }
    capture("fence_tank_panel");
    // Tank Adjustment: the wall up a step (for its price) and back; Drain
    // empties it, still a tank; Fill Tank with fresh water fills it again
    {
      const Fences::Exhibit *tex = nullptr;
      for (const Fences::Exhibit &x : fences.exhibits())
        if (x.tank)
          tex = &x;
      if (tex) {
        int top = tex->wallTop;
        double cash = g_sim.cash();
        click(4875);
        check("tank wall up a step, for its price",
              tex->wallTop == top + 1 &&
                  std::lround(cash - g_sim.cash()) == std::lround(fences.wallStepCost(tex->id)));
        check(("2 x 2 tank steps as the original ($" + std::to_string((int)fences.wallStepCost(tex->id)) +
               ", $" + std::to_string(fences.baseStepCost(tex->id)) + ")").c_str(),
              tex->tiles.size() != 4 || ((int)fences.wallStepCost(tex->id) == 206 &&
                                         fences.baseStepCost(tex->id) == 32));
        capture("tank_wall_up");
        // The buttons' tooltips: help 30000 + helpid, after resting a moment
        {
          UiElement *up = g_hud->getElementById(4875);
          SDL_Rect r = up ? up->getLastRect() : SDL_Rect{0, 0, 0, 0};
          g_hud->setMouse(r.x + r.w / 2, r.y + r.h / 2, SDL_GetTicks() - 1000);
          check(("wall up tooltip: " + g_hud->tooltipText()).c_str(),
                g_hud->tooltipText() == rm->getString(34875));
          capture("tank_tooltip");
          g_hud->setMouse(-1, -1, 0);
        }
        click(4874);
        SDL_Log("[PanelShots] tank ground %d floor %d top %d (was %d)", tex->groundHeight,
                tex->floorHeight, tex->wallTop, top);
        check("tank wall back down", tex->wallTop == top);
        click(4885);
        world->getFences().update(10.0f);
        capture("tank_drained_page");
        check("drained: no water, still a tank", tex->water == 0 && tex->tank);
        check(("drained 2 x 2 prices as the original ($" + std::to_string((int)fences.wallStepCost(tex->id)) +
               ", $" + std::to_string(fences.fillCost(tex->id, true)) + " / $" +
               std::to_string(fences.fillCost(tex->id, false)) + ")").c_str(),
              tex->tiles.size() != 4 || ((int)fences.wallStepCost(tex->id) == 200 &&
                                         fences.fillCost(tex->id, true) == 30 &&
                                         fences.fillCost(tex->id, false) == 20));
        click(4870);
        world->getFences().update(10.0f);
        capture("tank_fresh_page");
        check("filled with fresh water", tex->water == 1 && !tex->salt);
        // A filter beside it (one side or another that's clear), not two;
        // dirty water cleaned for its upkeep
        int tx0 = 1 << 30, ty0 = 1 << 30, tx1 = -1, ty1 = -1;
        for (auto [x, y] : tex->tiles) {
          tx0 = std::min(tx0, x); ty0 = std::min(ty0, y);
          tx1 = std::max(tx1, x); ty1 = std::max(ty1, y);
        }
        const std::pair<int, int> spots[] = {{tx1 + 1, ty0}, {tx0 - 2, ty0}, {tx0, ty1 + 1}, {tx0, ty0 - 2}};
        bool placed = false;
        std::pair<int, int> at;
        for (auto s : spots)
          if (!placed && fences.filterFit(s.first, s.second, world->getMap()) == Fences::Fit::Ok) {
            placed = fences.placeFilter(s.first, s.second, world->getMap());
            at = s;
          }
        check("a filter beside the tank", placed && fences.filters().size() == 1);
        bool second = false;
        for (auto s : spots)
          second = second || fences.filterFit(s.first, s.second, world->getMap()) == Fences::Fit::Ok;
        check("only one filter to a tank", !second);
        Fences::Exhibit *wex = fences.exhibit(tex->id);
        wex->purity = 50;
        wex->purityClock = 0;
        world->getFences().update(50.0f); // a point lost, one cleaning
        check(("filter cleans: purity " + std::to_string((int)wex->purity)).c_str(),
              (int)wex->purity == 59);
        check("filter upkeep $50", fences.takeUpkeep() == 50);
        wex->purity = 30;
        capture("tank_filter");
        wex->purity = 100;
        // and its placing preview, round the tank
        world->setFilterTool(true);
        int px, py;
        world->vertexToWindow(at.first + 1, at.second + 1, px, py);
        world->mouseMove(px, py);
        capture("tank_filter_preview");
        world->setFilterTool(false);
        // Its panel: name, status, upkeep; Sell gives back half its price
        g_hud->showFilter(0);
        capture("filter_panel");
        check("filter panel open", g_hud->isPanelOpen(46));
        click(4910);
        capture("filter_panel_finance");
        {
          double cash = g_sim.cash();
          click(4906);
          check("filter sold for half", fences.filters().empty() &&
                                            std::lround(g_sim.cash() - cash) == fences.filterType().cost / 2);
          check("its panel closes", !g_hud->isPanelOpen(46));
        }
      } else {
        check("a tank to adjust", false);
      }
    }
    g_hud->showPanel(0, "");
    // Drained first: deleting a wall still asks (as the original)
    for (const Fences::Exhibit &x : fences.exhibits())
      if (x.tank)
        fences.drainExhibit(x.id);
    world->setBulldozer(true);
    int bx, by, bx2, by2;
    world->vertexToWindow(tx + 2, ty, bx, by);
    world->vertexToWindow(tx + 2, ty + 1, bx2, by2);
    world->mouseMove((bx + bx2) / 2, (by + by2) / 2);
    handleToolResult(world->mouseDown((bx + bx2) / 2, (by + by2) / 2), rm);
    capture("fence_drain_ask");
    check("a drained tank's wall asks first", g_hud->hasDialog());
    double cashBefore = g_sim.cash();
    enter();
    check(("deleting a tank: the clicked wall's 80% back ($" +
           std::to_string(std::lround(g_sim.cash() - cashBefore)) + ")").c_str(),
          std::lround(g_sim.cash() - cashBefore) == 100);
    {
      // A $70 chain-link piece gives back $56 (measured)
      int fenceRefund = -1;
      for (const auto &kv : fences.pieces())
        if (fences.types()[kv.second.type].key == "chainlnk" && !kv.second.gate)
          fenceRefund = fences.refundOf(kv.first);
      check(("chain-link refund $" + std::to_string(fenceRefund)).c_str(), fenceRefund == 56);
    }
    world->setBulldozer(false);
    capture("fence_tank_drained");
    {
      // Every wall piece gone, the hole left
      int left = 0;
      for (int i = 0; i < 2; i++) {
        left += fences.at({true, tx + i, ty}) ? 1 : 0;
        left += fences.at({true, tx + i, ty + 2}) ? 1 : 0;
        left += fences.at({false, tx, ty + i}) ? 1 : 0;
        left += fences.at({false, tx + 2, ty + i}) ? 1 : 0;
      }
      const MapTile *in = world->getMap().getTile(tx, ty);
      const MapTile *out = world->getMap().getTile(tx - 1, ty);
      check(("deleting a tank wall takes the whole tank (" + std::to_string(left) + " left)").c_str(),
            left == 0);
      check("the hole stays", in && out && in->height < out->height);
    }
    check("exhibits made", fences.exhibitCount() == 2);
    check("on a fence: in the way (orange)",
          fences.fit({true, cx, cy}, world->getMap()) == Fences::Fit::InTheWay);
    check("outside the zoo: can't go (red)",
          fences.fit({true, 1, 1}, world->getMap()) == Fences::Fit::Outside);
    {
      World::ToolResult outside;
      // Any land outside the wall
      for (int y = 1; y < world->getMap().getHeight() && !outside.outsideZoo; y++)
        for (int x = 0; x < world->getMap().getWidth() && !outside.outsideZoo; x++)
          outside.outsideZoo = fences.outsideZooWall({true, x, y}, world->getMap());
      handleToolResult(outside, rm);
      capture("msg_zoo_wall");
      capture("msg_zoo_wall");
      check("outside the zoo wall: the message", outside.outsideZoo);
      g_hud->showMessage("", {255, 255, 255, 255}, 0);
      g_hud->setPausedMessage(true);
      capture("msg_paused");
      capture("msg_paused");
      g_hud->setPausedMessage(false);
    }
    check("open ground: can go (green)",
          fences.fit({true, cx + 1, cy + 1}, world->getMap()) == Fences::Fit::Ok);

    // A drag that wanders (as a mouse does): the fence follows it, with the
    // grid showing
    {
      world->setFenceTool(fences.typeIndex("chainlnk"));
      int sx, sy;
      world->vertexToWindow(cx - 4, cy - 4, sx, sy);
      handleToolResult(world->mouseDown(sx, sy), rm);
      for (int i = 1; i <= 6; i++) {
        int px, py;
        world->vertexToWindow(cx - 4 + i, cy - 4 + i / 2, px, py);
        world->mouseMove(px, py);
      }
      capture("fence_drag_grid");
      {
        std::string pv;
        for (auto &pp : fences.preview)
          pv += " " + std::string(pp.first.alongX ? "x" : "y") + std::to_string(pp.first.x) + "," + std::to_string(pp.first.y);
        SDL_Log("[PanelShots] drag from %d,%d:%s", cx - 4, cy - 4, pv.c_str());
      }
      check(("drag bends once (" + std::to_string(fences.preview.size()) + " pieces)").c_str(),
            fences.preview.size() == 9 && fences.preview.front().first.alongX &&
                !fences.preview.back().first.alongX);
      // The modes: bent the other way, straight, the rectangle
      world->cycleFenceMode(1);
      check("other way: y first", fences.preview.size() == 9 && !fences.preview.front().first.alongX);
      world->cycleFenceMode(1);
      check("straight: 6 pieces", fences.preview.size() == 6);
      world->cycleFenceMode(1);
      check(("rectangle: 18 pieces (" + std::to_string(fences.preview.size()) + ")").c_str(),
            fences.preview.size() == 18);
      capture("fence_drag_box");
      world->cycleFenceMode(1);
      world->cancelTool();
      // The switch off: the original's drag, following the mouse step by
      // step (a staircase here, not one bend)
      std::string listed = devCommand({"features"}, rm);
      check("console lists the switches", listed.find("fencemodes") != std::string::npos &&
                                              listed.find("staffpaths") != std::string::npos);
      check("console: fencemodes off", devCommand({"set", "fencemodes", "off"}, rm).find("off") != std::string::npos);
      handleToolResult(world->mouseDown(sx, sy), rm);
      for (int i = 1; i <= 6; i++) {
        int px, py;
        world->vertexToWindow(cx - 4 + i, cy - 4 + i / 2, px, py);
        world->mouseMove(px, py);
      }
      int turns = 0;
      for (size_t i = 1; i < fences.preview.size(); i++)
        turns += fences.preview[i].first.alongX != fences.preview[i - 1].first.alongX ? 1 : 0;
      check(("fencemodes off: follows the mouse (" + std::to_string(fences.preview.size()) + " pieces, " +
             std::to_string(turns) + " turns)").c_str(),
            fences.preview.size() == 9 && turns > 1);
      world->cancelTool();
      devCommand({"set", "fencemodes", "on"}, rm);
      world->setFenceTool(-1);
    }

    // Freeform: everything from the start (the Tank Filter first); the
    // original's timed release: in June the Tank Filter is there, first,
    // Black Bar and Glass fifth
    {
      ItemCatalog::get().setMonth(unlockMonthFor(1));
      auto all = ItemCatalog::get().inCategory("fence");
      check("all unlocked: Tank Filter first in January",
            !Features::allUnlocked || (!all.empty() && all[0]->file == "scenery/other/filter1.ai"));
      ItemCatalog::get().setMonth(6);
      auto list = ItemCatalog::get().inCategory("fence");
      check("June: Tank Filter first", !list.empty() && list[0]->file == "scenery/other/filter1.ai");
      check("June: Black Bar and Glass fifth", list.size() > 4 && list[4]->file == "fences/tankwall.ai");
      ItemCatalog::get().setMonth(1);
      auto jan = ItemCatalog::get().inCategory("fence");
      check("January: no Tank Filter", !jan.empty() && jan[0]->file == "fences/tankwal2.ai");
      ItemCatalog::get().setMonth(unlockMonthFor(1));
    }

    // A single click puts down one piece
    check("bulldozer starts off", !g_hud->bulldozerOn());
    {
      size_t had = fences.pieces().size();
      world->setFenceTool(fences.typeIndex("chainlnk"));
      int px, py, qx, qy;
      world->vertexToWindow(cx - 2, cy, px, py);
      world->vertexToWindow(cx - 2, cy + 1, qx, qy);
      int mx = (px + qx) / 2, my = (py + qy) / 2;
      world->mouseMove(mx, my);
      g_mouseX = mx;
      g_mouseY = my;
      capture("fence_hover");
      handleToolResult(world->mouseDown(mx, my), rm);
      handleToolResult(world->mouseUp(mx, my), rm);
      world->setFenceTool(-1);
      check("one click, one piece", fences.pieces().size() == had + 1);
    }
    // No tool: clicking a fence deletes nothing and opens nothing; the
    // exhibit's gate opens its information
    {
      size_t had = fences.pieces().size();
      int px, py, qx, qy;
      world->vertexToWindow(cx, cy + 1, px, py);
      world->vertexToWindow(cx, cy + 2, qx, qy);
      g_hud->showPanel(0, "");
      handleToolResult(world->mouseDown((px + qx) / 2, (py + qy) / 2 - 12), rm);
      world->mouseUp((px + qx) / 2, (py + qy) / 2 - 12);
      check("clicking a fence keeps it", fences.pieces().size() == had);
      check("clicking a plain fence opens nothing", !g_hud->isPanelOpen(30));
      // The gate: the drawn middle of the gate piece
      for (const auto &kv : fences.pieces())
        if (kv.second.gate && fences.exhibitOf(kv.first) >= 0) {
          const Fences::Edge &e = kv.first;
          world->vertexToWindow(e.x, e.y, px, py);
          world->vertexToWindow(e.alongX ? e.x + 1 : e.x, e.alongX ? e.y : e.y + 1, qx, qy);
          Fences::Edge got{false, -1, -1};
          bool hit = world->pickFence((px + qx) / 2, (py + qy) / 2 - 12, got);
          SDL_Log("[PanelShots] gate %d,%d,%d picked %d: %d,%d,%d exhibit %d", e.alongX, e.x, e.y,
                  hit, got.alongX, got.x, got.y, fences.exhibitOf(e));
          handleToolResult(world->mouseDown((px + qx) / 2, (py + qy) / 2 - 12), rm);
          world->mouseUp((px + qx) / 2, (py + qy) / 2 - 12);
          break;
        }
      check("clicking the gate opens its exhibit", g_hud->isPanelOpen(30));
      capture("fence_click_exhibit");
      g_hud->showPanel(0, "");
    }
  }

  // Birds over the zoo: a seagull and a crow crossing near the entrance
  {
    Ambient &amb = world->getAmbient();
    amb.clear();
    capture("ambient_none");
    int gull = amb.kindIndex("seagull"), crow = amb.kindIndex("crow");
    amb.spawnAt(gull, 16.0f, 38.0f, 1.0f, 0.3f, 30.0f);
    amb.spawnAt(crow, 20.0f, 44.0f, -0.6f, 0.8f, 30.0f);
    amb.update(0.3f, world->getMap());
    check("birds fly over", amb.count() >= 2);
    if (std::getenv("ZT_DUMP_AMBIENT")) {
      Animation *a = rm->getAnimation("animals/seagull/m/idle/idle");
      SDL_SetRenderDrawColor(renderer, 90, 70, 50, 255);
      SDL_RenderClear(renderer);
      SDL_RenderSetScale(renderer, 6, 6);
      for (int f = 0; f < 6; f++)
        a->drawAnchored(renderer, 20.0f + f * 36, 40.0f, CompassDirection::SE, nullptr, f);
      for (int f = 0; f < 6; f++)
        a->drawAnchored(renderer, 20.0f + f * 36, 90.0f, CompassDirection::SW, nullptr, f);
      SDL_RenderSetScale(renderer, 1, 1);
      int w = 0, h = 0;
      SDL_GetRendererOutputSize(renderer, &w, &h);
      SDL_Surface *sf = SDL_CreateRGBSurfaceWithFormat(0, w, h, 32, SDL_PIXELFORMAT_ARGB8888);
      SDL_RenderReadPixels(renderer, nullptr, SDL_PIXELFORMAT_ARGB8888, sf->pixels, sf->pitch);
      SDL_SaveBMP(sf, (outDir + "/seagull_frames.bmp").c_str());
      SDL_FreeSurface(sf);
    }
    capture("ambient_birds");
  }

  // Staff: a maintenance worker and a keeper hired with the tool (the
  // first month's pay at once), walking about; a worn fence fixed
  {
    Staff &st = world->getStaff();
    WorldMap &map = world->getMap();
    Fences &fn = world->getFences();
    int maintType = st.typeOfFile("staff/maint.ai"), keeperType = st.typeOfFile("staff/keeper.ai");
    // Open zoo ground near the entrance
    std::vector<std::pair<int, int>> spots;
    for (int r = 0; r < 20 && spots.size() < 2; r++)
      for (int dy = -r; dy <= r && spots.size() < 2; dy++)
        for (int dx = -r; dx <= r && spots.size() < 2; dx++) {
          int x = testSpotX + dx, y = testSpotY + dy;
          if (st.canPlace(x + 0.5f, y + 0.5f, map, fn) == Fences::Fit::Ok &&
              st.canPlace(x + 1.5f, y + 0.5f, map, fn) == Fences::Fit::Ok &&
              st.canPlace(x + 0.5f, y + 1.5f, map, fn) == Fences::Fit::Ok &&
              st.canPlace(x + 1.5f, y + 1.5f, map, fn) == Fences::Fit::Ok &&
              (spots.empty() || std::abs(spots[0].first - x) + std::abs(spots[0].second - y) > 3))
            spots.push_back({x, y});
        }
    check("staff types loaded", maintType >= 0 && keeperType >= 0 && st.types().size() >= 5);
    if (spots.size() == 2 && true) {
      double cash = g_sim.cash();
      int ids[2] = {-1, -1};
      int types[2] = {maintType, keeperType};
      for (int i = 0; i < 2; i++) {
        world->setStaffTool(types[i]);
        int px, py;
        world->vertexToWindow(spots[i].first + 1, spots[i].second + 1, px, py);
        world->mouseMove(px, py);
        if (i == 0)
          capture("staff_hire_ghost");
        World::ToolResult r = world->mouseDown(px, py);
        handleToolResult(r, rm);
        ids[i] = r.staff;
      }
      world->setStaffTool(-1);
      check(("hired, first month paid ($" + std::to_string(std::lround(cash - g_sim.cash())) + ")").c_str(),
            ids[0] >= 0 && ids[1] >= 0 && std::lround(cash - g_sim.cash()) == 1100);
      const Staff::Member *m0 = st.member(ids[0]);
      check(("named " + (m0 ? m0->name : std::string("?"))).c_str(),
            m0 && m0->name == "Maintenance Worker 1");
      capture("staff_hired");
      if (!st.member(ids[0]) || !st.member(ids[1])) {
        check("staff hired for the rest of the staff tests", false);
        goto staffDone;
      }
      float x0 = m0 ? m0->x : 0, y0 = m0 ? m0->y : 0;
      for (int i = 0; i < 60; i++)
        st.update(0.1f, map, fn);
      capture("staff_walking");
      m0 = st.member(ids[0]);
      check("staff walk about", m0 && std::hypot(m0->x - x0, m0->y - y0) > 0.3f);
      // Their panel and the list
      g_hud->showStaff(ids[0]);
      capture("staff_panel");

      click(3565);
      capture("staff_panel_duties");
      g_hud->showPanel(31, "");
      g_hud->refreshStaff();
      capture("staff_list");
      g_hud->showPanel(0, "");
      g_hud->showStaff(ids[1]);
      capture("staff_panel_keeper");
      g_hud->showPanel(0, "");
      // A worn fence: the maintenance worker comes and fixes it
      Fences::Edge worn{false, -1, -1};
      float bestD = 1e9f;
      for (const auto &kv : fn.pieces()) {
        if (kv.second.tank >= 0 || fn.types()[kv.second.type].indestructible)
          continue;
        float ex = kv.first.x + 0.5f, ey = kv.first.y + 0.5f;
        float d = std::hypot(ex - m0->x, ey - m0->y);
        if (d < bestD) {
          bestD = d;
          worn = kv.first;
        }
      }
      fn.setLife(worn, 3.0f);
      capture("fence_worn");
      bool fixing = false;
      for (int i = 0; i < 1200 && fn.worn(worn); i++) {
        st.update(0.1f, map, fn);
        const Staff::Member *mm = st.member(ids[0]);
        if (!fixing && mm && mm->dutyId == 10510) {
          fixing = true;
          capture("staff_fixing");
        }
      }
      check(("maintenance worker fixes a worn fence (life " + std::to_string((int)fn.lifeOf(worn)) + ")").c_str(),
            !fn.worn(worn) && fixing);
      if (std::getenv("ZT_STAFF_SHEET")) {
        // Every type, man and woman, a few colourings, standing and walking
        SDL_SetRenderDrawColor(renderer, 110, 90, 60, 255);
        SDL_RenderClear(renderer);
        SDL_RenderSetScale(renderer, 2, 2);
        const CompassDirection dirs[8] = {CompassDirection::N, CompassDirection::NE, CompassDirection::E,
                                          CompassDirection::SE, CompassDirection::S, CompassDirection::SW,
                                          CompassDirection::W, CompassDirection::NW};
        int row = 0;
        for (size_t t = 0; t < st.types().size(); t++)
          for (int sex = 0; sex < 2; sex++) {
            if (!st.types()[t].sexes[sex])
              continue;
            Staff::Member m;
            m.type = static_cast<int>(t);
            m.female = sex == 1;
            m.hair = row % 4;
            m.skin = row % 5;
            for (int d = 0; d < 8; d++) {
              if (Animation *a = st.art(m, "walk"))
                a->drawAnchored(renderer, 20.0f + d * 26, 34.0f + row * 34, dirs[d], nullptr, 2);
              if (Animation *a = st.art(m, d < 4 ? "idle" : (d < 6 ? "fix" : "feedh")))
                a->drawAnchored(renderer, 240.0f + d * 26, 34.0f + row * 34, CompassDirection::SE, nullptr, d);
            }
            row++;
          }
        SDL_RenderSetScale(renderer, 1, 1);
        int w = 0, h = 0;
        SDL_GetRendererOutputSize(renderer, &w, &h);
        SDL_Surface *sf = SDL_CreateRGBSurfaceWithFormat(0, w, h, 32, SDL_PIXELFORMAT_ARGB8888);
        SDL_RenderReadPixels(renderer, nullptr, SDL_PIXELFORMAT_ARGB8888, sf->pixels, sf->pitch);
        SDL_SaveBMP(sf, (outDir + "/staff_sheet.bmp").c_str());
        SDL_FreeSurface(sf);
      }
      int wagesBefore = st.monthlyWages();
      check("monthly wages $1100", wagesBefore == 1100);
      // DEV CONSOLE states: a keeper looks after an exhibit with fake
      // animals (food down, dung raked), a marine specialist cleans a
      // dirty tank, a maintenance worker sweeps litter
      {
        auto cmd = [&](const std::string &line) {
          std::vector<std::string> words;
          std::stringstream ss(line);
          std::string word;
          while (ss >> word)
            words.push_back(word);
          std::string out = devCommand(words, rm);
          SDL_Log("[Console] %s -> %s", line.c_str(), out.c_str());
          return out;
        };
        cmd("paths on");
        cmd("exhibits");
        // The first land exhibit: 3 fake zoo animals, dung, no food
        int land = -1, landNo = 0, n = 0;
        for (const Fences::Exhibit &e : fn.exhibits())
          if (e.named) {
            n++;
            if (!e.tank && land < 0) {
              land = e.id;
              landNo = n;
            }
          }
        check("a land exhibit to test with", land >= 0);
        std::string ex = std::to_string(landNo);
        check("console: fake animals", cmd("animals " + ex + " 3 zoo herb").find("3 fake zoo") != std::string::npos);
        cmd("dung " + ex + " 3");
        cmd("nofood " + ex);
        // The keeper hired above
        int keeperNo = 0;
        for (size_t i = 0; i < st.members().size(); i++)
          if (st.members()[i].id == ids[1])
            keeperNo = static_cast<int>(i) + 1;
        check("console: keeper assigned", cmd("assign " + std::to_string(keeperNo) + " " + ex).find("looks after") != std::string::npos);
        bool entered = false, fed = false, raked = false, shotFeeding = false, shotGoing = false,
             shotRaking = false;
        for (int i = 0; i < 3000 && !(fed && raked && !entered); i++) {
          st.update(0.1f, map, fn);
          world->getItems().update(0.1f, fn);
          const Staff::Member *k = st.member(ids[1]);
          if (k && k->dutyId == 10303 && !k->path.empty() && !shotGoing) {
            shotGoing = true;
            world->centreOnStaff(ids[1]);
            capture("path_keeper_to_gate");
          }
          if (k && k->dutyId == 10307 && !entered) {
            entered = true;
          }
          if (k && k->dutyId == 10302 && !k->path.empty() && !shotRaking) {
            shotRaking = true;
            world->centreOnStaff(ids[1]);
            capture("path_keeper_raking");
          }
          if (k && k->dutyId == 10300 && !shotFeeding) {
            shotFeeding = true;
            world->centreOnStaff(ids[1]);
            capture("keeper_feeding");
          }
          fed = world->getItems().foodIn(land) > 0;
          raked = world->getItems().ofKind(ZooItems::Kind::Dung, land).empty();
          if (fed && raked && k && k->job == Staff::Job::None)
            break;
        }
        check(("keeper fed the exhibit (food " + std::to_string((int)world->getItems().foodIn(land)) + ")").c_str(), fed);
        check("keeper raked the dung", raked);
        capture("keeper_visit_done");
        // Litter: the maintenance worker sweeps it
        world->centreOnStaff(ids[0]);
        cmd("litter 3");
        size_t litter = world->getItems().ofKind(ZooItems::Kind::Litter).size();
        bool shotSweep = false;
        for (int i = 0; i < 4000 && !world->getItems().ofKind(ZooItems::Kind::Litter).empty(); i++) {
          st.update(0.1f, map, fn);
          const Staff::Member *mw = st.member(ids[0]);
          if (!shotSweep && mw && mw->job == Staff::Job::Litter && mw->path.size() > 0) {
            shotSweep = true;
            world->centreOnStaff(ids[0]);
            capture("path_maint_to_litter");
          }
        }
        check(("maintenance worker swept " + std::to_string(litter) + " litter").c_str(),
              litter > 0 && world->getItems().ofKind(ZooItems::Kind::Litter).empty());
        // A tank with fake marine animals and dirty water: a marine
        // specialist feeds it and cleans the water from the platform
        {
          int tx0 = -1, ty0 = -1;
          for (int r = 3; r < 30 && tx0 < 0; r++)
            for (int dy = -r; dy <= r && tx0 < 0; dy++)
              for (int dx = -r; dx <= r && tx0 < 0; dx++) {
                int x0 = 20 + dx, y0 = 36 + dy;
                bool ok = true;
                for (int y = y0 - 1; y <= y0 + 3 && ok; y++)
                  for (int x = x0 - 1; x <= x0 + 3 && ok; x++)
                    ok = st.canPlace(x + 0.5f, y + 0.5f, map, fn) == Fences::Fit::Ok &&
                         !fn.at({true, x, y}) && !fn.at({false, x, y});
                if (ok) {
                  tx0 = x0;
                  ty0 = y0;
                }
              }
          int tankType = fn.typeIndex("atltank");
          int tankNo = 0;
          if (tx0 >= 0 && tankType >= 0) {
            for (int i = 0; i < 2; i++) {
              fn.place({true, tx0 + i, ty0}, tankType, 99, map);
              fn.place({true, tx0 + i, ty0 + 2}, tankType, 99, map);
              fn.place({false, tx0, ty0 + i}, tankType, 99, map);
              fn.place({false, tx0 + 2, ty0 + i}, tankType, 99, map);
            }
            for (int id : fn.updateExhibits(map, 1, 0, 1))
              fn.finishExhibit(id, "Test Tank", map);
            int k = 0;
            for (const Fences::Exhibit &e : fn.exhibits())
              if (e.named) {
                k++;
                if (e.name == "Test Tank")
                  tankNo = k;
              }
          }
          check("a tank to test with", tankNo > 0);
          int trainerType = st.typeOfFile("staff/trainer.ai");
          int trainer = -1;
          for (int r = 1; r < 10 && trainer < 0 && tx0 >= 0; r++)
            for (int dx = -r; dx <= r && trainer < 0; dx++)
              trainer = st.hire(trainerType, tx0 + 1.5f + dx, ty0 - r + 0.5f, map, fn);
          int trainerNo = 0;
          for (size_t i = 0; i < st.members().size(); i++)
            if (st.members()[i].id == trainer)
              trainerNo = static_cast<int>(i) + 1;
          std::string tn = std::to_string(tankNo);
          check("marine specialist: not to an empty tank",
                cmd("assign " + std::to_string(trainerNo) + " " + tn).find("empty") != std::string::npos);
          cmd("animals " + tn + " 2");
          cmd("dirty " + tn + " 30");
          check("marine specialist assigned", cmd("assign " + std::to_string(trainerNo) + " " + tn).find("looks after") != std::string::npos);
          const Fences::Exhibit *te = nullptr;
          for (const Fences::Exhibit &e : fn.exhibits())
            if (e.name == "Test Tank")
              te = &e;
          bool shot = false;
          for (int i = 0; i < 4000 && te && (te->purity < 100.0f || world->getItems().foodIn(te->id) <= 0); i++) {
            st.update(0.1f, map, fn);
            const Staff::Member *ms = st.member(trainer);
            if (!shot && ms && ms->dutyId == 10327) {
              shot = true;
              world->centreOnStaff(trainer);
              capture("marine_cleaning");
            }
          }
          check(("marine specialist cleaned the tank (purity " + std::to_string(te ? (int)te->purity : -1) + ")").c_str(),
                te && te->purity >= 100.0f);
          check("marine specialist fed the tank", te && world->getItems().foodIn(te->id) > 0);
        }
        g_console.toggle();
        g_console.print("> exhibits");
        g_console.print(cmd("exhibits"));
        g_console.print("> staff");
        g_console.print(cmd("staff"));
        capture("dev_console");
        g_console.toggle();
      }
      // Where everything is: rocks and trees block tiles; staff are found
      // where they stand. A* routes round what's in the way, straightened.
      {
        world->reindex();
        WorldIndex &ix = world->getIndex();
        int blocked = 0;
        for (int y = 0; y < map.getHeight(); y++)
          for (int x = 0; x < map.getWidth(); x++)
            blocked += ix.blocked(x, y) ? 1 : 0;
        check(("index: rocks and trees block " + std::to_string(blocked) + " tiles").c_str(), blocked > 20);
        const Staff::Member *m0i = st.member(ids[0]);
        bool found = false;
        for (const WorldIndex::Mover &mv : ix.near(m0i->x, m0i->y, 1.0f))
          found = found || mv.id == ids[0];
        check("index: knows where staff are", found);
        // A route across open ground comes out as a few straight legs
        Staff::Member probe = *m0i;
        float sx = probe.x, sy = probe.y;
        int bestLen = 0;
        size_t legs = 0;
        for (int dy = -8; dy <= 8; dy++)
          for (int dx = -8; dx <= 8; dx++) {
            int gx = static_cast<int>(sx) + dx, gy = static_cast<int>(sy) + dy;
            if (std::abs(dx) + std::abs(dy) < bestLen || st.canPlace(gx + 0.5f, gy + 0.5f, map, fn) != Fences::Fit::Ok)
              continue;
            std::vector<std::pair<int, int>> tiles;
            auto pass = [&](int x, int y) { return st.canPlace(x + 0.5f, y + 0.5f, map, fn) == Fences::Fit::Ok; };
            auto step = [&](int, int, int, int) { return true; };
            if (Pathfinder::find(map.getWidth(), map.getHeight(), static_cast<int>(sx), static_cast<int>(sy), gx, gy, pass, step, tiles)) {
              std::vector<std::pair<float, float>> pts;
              for (auto &t : tiles)
                pts.push_back({t.first + 0.5f, t.second + 0.5f});
              Pathfinder::smooth(pts, pass, step);
              bestLen = std::abs(dx) + std::abs(dy);
              legs = pts.size();
            }
          }
        check(("A*: a route " + std::to_string(bestLen) + " tiles off in " + std::to_string(legs) + " points").c_str(),
              bestLen >= 8 && legs >= 2 && legs <= 6);
      }
      // The DRT base: its pad and the helicopter on it, $2000 a month
      {
        int drt = st.typeOfFile("scenery/building/helibase.ai");
        int wagesNow = st.monthlyWages();
        int at = -1;
        for (int r = 2; r < 30 && at < 0; r++)
          for (int dy = -r; dy <= r && at < 0; dy++)
            for (int dx = -r; dx <= r && at < 0; dx++) {
              float x = 22.0f + dx, y = 32.0f + dy;
              if (st.canPlaceType(drt, x, y, map, fn) == Fences::Fit::Ok)
                at = st.hire(drt, x, y, map, fn);
            }
        const Staff::Member *h = st.member(at);
        check(("DRT base hired (" + (h ? h->name : std::string("none")) + ")").c_str(),
              h && st.monthlyWages() == wagesNow + 2000);
        if (h)
          world->centreOn(h->x, h->y);
        capture("staff_drt");
      }
    } else {
      check("open ground for staff", false);
    }
  staffDone:;
  }

  // Paths: laid with the tool along the cursor's way (an L), $ a tile,
  // green preview; the bulldozer lifts one for 80% back
  {
    WorldMap &map = world->getMap();
    int x0 = -1, y0 = -1;
    for (int r = 0; r < 25 && x0 < 0; r++)
      for (int dy = -r; dy <= r && x0 < 0; dy++)
        for (int dx = -r; dx <= r && x0 < 0; dx++) {
          int x = testSpotX + dx, y = testSpotY + dy;
          bool ok = true;
          for (int i = 0; i <= 5 && ok; i++)
            ok = world->pathFit(x + i, y) == Fences::Fit::Ok && world->pathFit(x + 5, y + i) == Fences::Fit::Ok &&
                 !map.isPath(x + i, y) && !map.isPath(x + 5, y + i);
          if (ok) {
            x0 = x;
            y0 = y;
          }
        }
    check("ground for a test path", x0 >= 0);
    if (x0 >= 0) {
      world->debugPaths = false;
      world->getStaff().logRoutes = false;
      world->centreOn(x0 + 3.0f, y0 + 3.0f);
      world->setPathTool("dirtpath", 10);
      auto at = [&](float tx, float ty, int &px, int &py) {
        world->vertexToWindow(static_cast<int>(tx), static_cast<int>(ty), px, py);
        int qx, qy;
        world->vertexToWindow(static_cast<int>(tx) + 1, static_cast<int>(ty) + 1, qx, qy);
        px = (px + qx) / 2;
        py = (py + qy) / 2;
      };
      int px, py;
      at(x0, y0, px, py);
      world->mouseMove(px, py);
      double cash = g_sim.cash();
      handleToolResult(world->mouseDown(px, py), rm);
      for (int i = 1; i <= 5; i++) {
        at(x0 + i, y0, px, py);
        world->mouseMove(px, py);
      }
      for (int i = 1; i <= 5; i++) {
        at(x0 + 5, y0 + i, px, py);
        world->mouseMove(px, py);
      }
      g_mouseX = px;
      g_mouseY = py;
      {
        int sx0, sy0;
        at(x0, y0, sx0, sy0);
        SDL_Log("[PanelShots] path L from %d,%d (window %d,%d) to window %d,%d, preview %zu", x0, y0, sx0, sy0, px, py,
                (size_t)0);
      }
      capture("path_drag_preview");
      handleToolResult(world->mouseUp(px, py), rm);
      int laid = 0;
      for (int i = 0; i <= 5; i++)
        laid += (map.isPath(x0 + i, y0) ? 1 : 0) + (i > 0 && map.isPath(x0 + 5, y0 + i) ? 1 : 0);
      check(("path follows the drag: an L of " + std::to_string(laid) + " tiles").c_str(), laid == 11);
      check(("path cost $" + std::to_string(std::lround(cash - g_sim.cash()))).c_str(),
            std::lround(cash - g_sim.cash()) == 110);
      world->setPathTool("", 0);
      world->mouseMove(0, 0);
      capture("path_laid");
      // The bulldozer lifts a tile, 80% back
      world->setBulldozer(true);
      world->setPathTool("", 10);
      at(x0 + 2, y0, px, py);
      cash = g_sim.cash();
      handleToolResult(world->mouseDown(px, py), rm);
      world->setBulldozer(false);
      check(("bulldozer lifts a path tile (+$" + std::to_string(std::lround(g_sim.cash() - cash)) + ")").c_str(),
            !map.isPath(x0 + 2, y0) && std::lround(g_sim.cash() - cash) == 8);
      map.setPath(x0 + 2, y0, "dirtpath");
    }
  }

  // Elevated paths: on flat ground, height +3, a drag of 9 tiles: three
  // stair tiles up, then level deck; off its end at height 0, three down
  {
    WorldMap &map = world->getMap();
    world->debugPaths = false;
    int lx = -1, ly = -1;
    for (int y = 4; y < map.getHeight() - 4 && lx < 0; y++)
      for (int x = 4; x < map.getWidth() - 16 && lx < 0; x++) {
        const MapTile *t0 = map.getTile(x, y);
        bool ok = t0 != nullptr;
        for (int i = 0; i < 14 && ok; i++)
          for (int j = -1; j <= 1 && ok; j++) {
            const MapTile *t = map.getTile(x + i, y + j);
            ok = t && world->pathFit(x + i, y + j) == Fences::Fit::Ok && !map.isPath(x + i, y + j);
            for (const PlacedObjects::Object &o : world->getObjects().objects())
              if (ok && static_cast<int>(std::floor(o.x)) == x + i && static_cast<int>(std::floor(o.y)) == y + j)
                ok = false;
            for (int c = 0; c < 4 && ok; c++)
              ok = t->cornerHeight[c] == t0->cornerHeight[0];
          }
        if (ok) {
          lx = x;
          ly = y;
        }
      }
    check("flat ground for a walkway", lx >= 0);
    if (lx >= 0) {
      world->centreOn(lx + 7.0f, static_cast<float>(ly));
      auto at = [&](int tx, int ty, int &px, int &py) {
        int qx, qy;
        world->vertexToWindow(tx, ty, px, py);
        world->vertexToWindow(tx + 1, ty + 1, qx, qy);
        px = (px + qx) / 2;
        py = (py + qy) / 2;
      };
      world->setPathTool("path", 25);
      world->adjustBuildHeight(3);
      int px, py;
      at(lx, ly, px, py);
      world->mouseMove(px, py);
      double cash = g_sim.cash();
      handleToolResult(world->mouseDown(px, py), rm);
      for (int i = 1; i <= 9; i++) {
        at(lx + i, ly, px, py);
        world->mouseMove(px, py);
      }
      g_mouseX = px;
      g_mouseY = py;
      capture("walkway_preview");
      handleToolResult(world->mouseUp(px, py), rm);
      Walkways &ww = world->getWalkways();
      const MapTile *g = map.getTile(lx, ly);
      int ground = g->cornerHeight[0];
      std::string shape;
      for (int i = 1; i <= 9; i++) {
        const Walkways::Deck *d = ww.at(lx + i, ly);
        shape += d ? std::to_string(*std::max_element(d->h, d->h + 4) - ground) : std::string("-");
      }
      check(("walkway climbs then runs level (heights " + shape + ")").c_str(), shape == "123333333");
      check(("walkway cost $" + std::to_string(std::lround(cash - g_sim.cash()))).c_str(),
            std::lround(cash - g_sim.cash()) == 25 + 9 * 50);
      // Back down off its end: height 0, a drag from the deck end
      world->adjustBuildHeight(-3);
      // (on the deck as drawn: three units up)
      world->pointToWindow(lx + 9.5f, ly + 0.5f, ground + 3.0f, px, py);
      world->mouseMove(px, py);
      handleToolResult(world->mouseDown(px, py), rm);
      for (int i = 10; i <= 13; i++) {
        at(lx + i, ly, px, py);
        world->mouseMove(px, py);
      }
      handleToolResult(world->mouseUp(px, py), rm);
      std::string down;
      for (int i = 10; i <= 13; i++) {
        const Walkways::Deck *d = ww.at(lx + i, ly);
        down += d ? std::to_string(*std::max_element(d->h, d->h + 4) - ground) : (map.isPath(lx + i, ly) ? "p" : "-");
      }
      // Three stair tiles down, the last one's far edge on the ground
      const Walkways::Deck *foot = ww.at(lx + 12, ly);
      check(("walkway comes back down (" + down + ")").c_str(),
            down.substr(0, 3) == "321" && foot && foot->h[CORNER_X1Y0] == ground && foot->h[CORNER_X1Y1] == ground);
      world->setPathTool("", 0);
      world->mouseMove(0, 0);
      capture("walkway_built");
      Features::elevatedPaths = false;
      capture("walkway_hidden");
      Features::elevatedPaths = true;
      {
        // One lone deck tile, flat, three units up (to look at closely)
        Walkways::Deck lone;
        lone.type = "path";
        for (int c = 0; c < 4; c++)
          lone.h[c] = ground + 3;
        world->getWalkways().set(lx + 4, ly + 3, lone);
        // a pair of joined flat decks, and a lone stair (rising +x)
        world->getWalkways().set(lx + 6, ly + 3, lone);
        world->getWalkways().set(lx + 7, ly + 3, lone);
        Walkways::Deck stair = lone;
        Walkways::shape(1, ground + 2, ground + 3, stair.h);
        world->getWalkways().set(lx + 2, ly + 3, stair);
        world->centreOn(lx + 4.5f, ly + 3.5f);
        capture("walkway_lone");
        world->getWalkways().remove(lx + 4, ly + 3);
        world->getWalkways().remove(lx + 6, ly + 3);
        world->getWalkways().remove(lx + 7, ly + 3);
        world->getWalkways().remove(lx + 2, ly + 3);
        world->centreOn(lx + 7.0f, static_cast<float>(ly));
      }
      // A worker at the foot of the stairs, sent past the far end: up the
      // stairs, along the deck, down the other side (decks count as path)
      Staff &st = world->getStaff();
      int maint = st.typeOfFile("staff/maint.ai");
      int id = st.hire(maint, lx + 0.5f, ly + 0.5f, map, world->getFences());
      check("a worker at the foot of the walkway", id >= 0);
      if (id >= 0) {
        st.logRoutes = true;
        bool sent = st.walkTo(id, lx + 13, ly, map, world->getFences());
        const Staff::Member *m = st.member(id);
        int deckPoints = 0;
        for (int l : m->pathLayers)
          deckPoints += l;
        check(("route over the walkway (" + std::to_string(deckPoints) + " deck points)").c_str(),
              sent && deckPoints >= 8);
        bool shot = false;
        float topZ = 0;
        for (int i = 0; i < 600 && st.member(id)->pathAt < st.member(id)->path.size(); i++) {
          st.update(0.05f, map, world->getFences());
          const Staff::Member *mm = st.member(id);
          topZ = std::max(topZ, st.heightOf(*mm, map) - ground);
          if (!shot && mm->layer == 1 && mm->x > lx + 5) {
            shot = true;
            capture("walkway_staff");
          }
        }
        check(("worker walked up on the deck (+" + std::to_string(static_cast<int>(topZ)) + ")").c_str(),
              topZ >= 2.9f);
        st.logRoutes = false;
        st.fire(id);
      }
    }
  }

  // What was done in the original: in February, marketing at $200 for 2
  // seconds; in April, research at $400 (min) for 26 seconds. The finance
  // page then shows -$1 and -$28.
  // (January 31 days, February 28, March 31, then into April)
  const double day = ZooSim::kSecondsPerDay;
  g_sim.update(day * 45);
  g_hud->showPanel(14, "");
  g_hud->showTab(14, 4104);
  click(4053);
  g_sim.update(2.0);
  click(4052);
  g_sim.update(day * (14 + 31 + 10));
  g_hud->showPanel(15, "");
  g_hud->showTab(15, 4009);
  click(4004);
  g_sim.update(26.0);
  click(4003);
  if (g_sim.changed()) {
    updateHudClock(rm);
    g_hud->setZooInfo(g_hud->zooInfo());
  }
  g_hud->showPanel(14, "");
  g_hud->showTab(14, 4154);
  capture("zoo_finance_apr");
  g_hud->showTab(14, 4153);
  g_hud->showTab(14, 4106);
  capture("zoo_rating_apr");

  // The game menu: Main Menu and Exit Game come back as their actions; ESC
  // closes and opens it
  g_hud->showPanel(0, "");
  g_hud->toggleGameMenu();
  capture("escmenu2");
  check("main menu button", click(1503) == UiAction::GAME_MAIN_MENU);
  check("exit game button", click(1504) == UiAction::GAME_EXIT);
  g_hud->toggleGameMenu();
  check("esc closes the game menu", !g_hud->isPanelOpen(5));
  g_hud->toggleGameMenu();
  check("esc opens the game menu", g_hud->isPanelOpen(5));

  if (timings)
    fclose(timings);
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
  g_timedUnlocks = config.getTimedUnlocks();
  Features::allUnlocked = !g_timedUnlocks;
  ResourceManager resource_manager(&config);
  g_console.run = [&resource_manager](const std::vector<std::string> &words) {
    return devCommand(words, &resource_manager);
  };

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
  // What the in-game screen needs that doesn't depend on the map (every
  // buyable item, the research programs) is read while the menus show, so
  // starting a game doesn't wait for it
  std::thread([rm = &resource_manager] {
    ItemCatalog::get().load(rm);
    Research::get().load(rm);
  }).detach();

  // ZT_DUMP_CATALOG=1: log every buy tab's list and quit (for comparing
  // with the original)
  if (std::getenv("ZT_DUMP_CATALOG")) {
    ItemCatalog::get().load(&resource_manager);
    Research::get().load(&resource_manager); // let the preload finish
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
    // The dev console takes the keyboard while it's open
    bool consoleKeys = false;
    if (g_currentState == LayoutState::GAME_LOOP && g_hud && !UiText::isEditing()) {
      consoleKeys = g_console.handleInputs(inputs);
      if (consoleKeys)
        inputs.erase(std::remove_if(inputs.begin(), inputs.end(),
                                    [](const Input &in) {
                                      return in.type != InputType::POSITIONED ||
                                             in.event == InputEvent::SCROLL_UP ||
                                             in.event == InputEvent::SCROLL_DOWN;
                                    }),
                     inputs.end());
    }

    for (Input input : inputs) {
      if (input.event == InputEvent::QUIT) {
        running = 0;
      }
      // ESC in a game opens (or closes) the game menu, as in the original
      if (input.event == InputEvent::KEY_ESCAPE && !UiText::isEditing() &&
          g_currentState == LayoutState::GAME_LOOP && g_hud)
        g_hud->toggleGameMenu();
      // PageUp / PageDown: the height paths are laid at (elevated paths)
      if (input.event == InputEvent::KEY_DOWN &&
          (input.key == SDLK_PAGEUP || input.key == SDLK_PAGEDOWN) && !UiText::isEditing() &&
          g_currentState == LayoutState::GAME_LOOP && g_world && !g_world->getPathTool().empty() &&
          Features::elevatedPaths)
        g_world->adjustBuildHeight(input.key == SDLK_PAGEUP ? 1 : -1);
      // Tab (Shift+Tab back) changes how a fence drag lays out
      if (input.event == InputEvent::KEY_DOWN && input.key == SDLK_TAB &&
          !UiText::isEditing() && g_currentState == LayoutState::GAME_LOOP && g_world &&
          g_world->getFenceTool() >= 0 && Features::fenceModes)
        g_world->cycleFenceMode((SDL_GetModState() & KMOD_SHIFT) ? -1 : 1);
    }

    if (layout) {
      std::vector<Input> layoutInputs = inputs;
      mapInputsToLayout(layoutInputs, getUiTransform(window.renderer));
      action = layout->handleInputs(layoutInputs);
    } else if (g_currentState == LayoutState::GAME_LOOP && g_hud) {
      bool dialogBefore = g_hud->hasDialog();
      action = hudInputs(window.renderer, inputs);
      if (!dialogBefore && !g_hud->hasDialog())
        worldInputs(window.renderer, &resource_manager, inputs);
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
      prepareHud(&resource_manager);
      break;

    case UiAction::STARTUP_PLAY_SCENARIO:
      delete layout;
      lyt_reader = resource_manager.getIniReader(
          getLayoutPath(&resource_manager, "scenario.lyt"));
      layout = new UiLayout(lyt_reader, &resource_manager);
      g_currentState = LayoutState::SCENARIO_SELECT;
      populateScenarioList(layout, g_scenarioManager);
      break;

    // The game menu: Exit Game quits; Main Menu leaves the game
    case UiAction::GAME_EXIT:
      running = false;
      break;
    case UiAction::GAME_MAIN_MENU:
      destroyHud();
      [[fallthrough]];
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
        g_hud->setPausedMessage(pause);
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
      static const Uint8 noKeys[SDL_NUM_SCANCODES] = {};
      g_world->update(g_console.isOpen() ? noKeys : state, deltaTime * g_gameSpeed);
      // The clock runs while the game isn't paused
      if (!g_world->isPaused())
        g_sim.update(deltaTime * g_gameSpeed);
      // Tank filters bill each time they clean
      if (int upkeep = g_world->getFences().takeUpkeep())
        g_sim.spend(ZooSim::Upkeep, upkeep);
      if (g_sim.changed()) {
        // A new month can unlock things to buy
        // Filters: this month's upkeep becomes last month's
        int months = static_cast<int>(g_sim.history().size());
        if (months != g_world->getFences().month()) {
          if (g_world->getFences().month() > 0) {
            g_world->getFences().newMonth();
            // Staff are paid every month
            if (int wages = g_world->getStaff().monthlyWages())
              g_sim.spend(ZooSim::Wages, wages);
          }
          g_world->getFences().setMonth(months);
        }
        int month = unlockMonthFor(static_cast<int>(g_sim.history().size()));
        if (month != ItemCatalog::get().getMonth()) {
          ItemCatalog::get().setMonth(month);
          if (g_hud)
            g_hud->refreshCatalog();
        }
        updateHudClock(&resource_manager);
        if (g_hud) {
          UiGameScreen::ZooInfo info = g_hud->zooInfo();
          info.staffCount = static_cast<int>(g_world->getStaff().members().size());
          g_hud->setZooInfo(info);
        }
      }
      // SDL_Log("MainLoop: Post-Update");

      // SDL_Log("MainLoop: Pre-Draw");
      g_world->setView(gameViewRect(window.renderer),
                       g_widescreen ? 1.0f
                                    : getUiTransform(window.renderer).scale);
      g_world->draw(window.renderer);
      updateZoomButtons(g_world); // the mouse wheel zooms too
      drawHud(window.renderer, &resource_manager);
      drawToolCost(window.renderer, &resource_manager);
      {
        int ow = 0, oh = 0;
        SDL_GetRendererOutputSize(window.renderer, &ow, &oh);
        g_console.draw(window.renderer, &resource_manager, ow, oh);
      }
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