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
#include "Sound.hpp"
#include "SaveGame.hpp"
#include "Goals.hpp"
#include "SDL_image.h"

#include "ui/UiImage.hpp"
#include "ui/UiLayout.hpp"
#include "ui/UiListBox.hpp"
#include "ui/UiButton.hpp"
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
static int g_esthetic = 0; // the zoo rating's esthetics term (0-10)

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

// The Snap Shot button (1099): 1 the map alone, 2 (Shift held) the screen as it is
static int g_snapshot = 0;

static void createHud(ResourceManager *rm, World *world, int startingCash) {
  destroyHud();
  // The HUD and its panels, as the original's in-game screen
  g_hud = g_nextHud ? g_nextHud
                    : new UiGameScreen(rm, "ui/main.lyt", "ui/gamescrn.lyt");
  g_nextHud = nullptr;

  // Undo starts visible but greyed out (nothing to undo yet); the Scenario
  // button is greyed out in freeform games
  if (UiButton *snap = dynamic_cast<UiButton *>(g_hud->getElementById(1099)))
    snap->onClick = [] { g_snapshot = (SDL_GetModState() & KMOD_SHIFT) ? 2 : 1; };
  if (UiButton *undo = dynamic_cast<UiButton *>(g_hud->getElementById(1075))) {
    undo->setHidden(false);
    undo->setDisabled(true);
    // Undo: the last thing done (since the last click on the map) undone,
    // its money back (or a bulldozing's refund taken back)
    undo->onClick = [] {
      if (!g_world || !g_world->canUndo())
        return;
      World::UndoMoney m = g_world->undo();
      if (m.construction)
        g_sim.spend(ZooSim::Construction, -m.construction);
      if (m.animals)
        g_sim.spend(ZooSim::AnimalPurchase, -m.animals);
      if (m.wages)
        g_sim.spend(ZooSim::Wages, -m.wages);
      if (m.recycling)
        g_sim.earn(ZooSim::Recycling, m.recycling);
    };
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
  // The Paths tab's walkway height arrows (elevatedpaths)
  g_hud->walkwayHeight = [world](int step) {
    if (Features::elevatedPaths)
      world->adjustBuildHeight(step);
  };
  g_hud->canRaise = [](const std::string &file) {
    if (!Features::elevatedPaths)
      return false;
    size_t slash = file.find_last_of('/'), dot = file.find_last_of('.');
    return World::raisable(file.substr(slash + 1, dot - slash - 1));
  };
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
  g_hud->exhibitsExist = [world] {
    for (const Fences::Exhibit &e : world->getFences().exhibits())
      if (e.named)
        return true;
    return false;
  };
  // The terraform page's Accept and Undo
  g_hud->terraformCommand = [world, rm](bool accept) {
    TerrainTool &tt = world->getTerrainTool();
    float c = tt.cost();
    if (accept && c > 0 && c <= g_sim.cash()) {
      g_sim.spend(ZooSim::Construction, tt.accept());
      updateHudClock(rm);
    } else if (!accept) {
      tt.undo();
    }
    world->getAnimals().markDirty();
  };
  // Buildings bought: their panel; sold for 80% of their price back
  g_hud->setObjects(&world->objectsMutable(), [world, rm](int id) {
    PlacedObjects &po = world->objectsMutable();
    int i = po.indexOf(id);
    if (i < 0)
      return;
    std::string file = PlacedObjects::fileOf(po.objects()[i]);
    for (const CatalogItem &item : ItemCatalog::get().all())
      if (item.file == file)
        g_sim.earn(ZooSim::Recycling, std::lround(item.cost * 0.8));
    po.remove(i);
    world->reindex();
    world->getAnimals().markDirty();
    updateHudClock(rm);
  });
  g_hud->setGuests(&world->getGuests(), [world, rm](int type) {
    const auto &types = world->getAnimals().types();
    return type >= 0 && type < static_cast<int>(types.size()) ? rm->getString(types[type].nameId) : std::string();
  });
  // Zookeeper Recommendations and Exhibit Suitability
  g_hud->animalAdvice = [world](int id) {
    return world->getAnimals().advice(id, world->getMap(), world->getFences());
  };
  g_hud->animalSuitability = [world](int id) {
    return world->getAnimals().suitability(id, world->getMap(), world->getFences());
  };
  // Animal Information's buttons
  g_hud->setAnimals(&world->getAnimals(), [world, rm](UiGameScreen::AnimalRequest r, int id) {
    switch (r) {
    case UiGameScreen::AnimalRequest::PickUp: world->pickUpAnimal(id); break;
    case UiGameScreen::AnimalRequest::Sell: {
      int refund = world->getAnimals().refund(id);
      world->sellAnimal(id);
      if (refund > 0)
        g_sim.earn(ZooSim::AnimalPurchase, refund);
      updateHudClock(rm);
      break;
    }
    case UiGameScreen::AnimalRequest::Select: world->centreOnAnimal(id); break;
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
static SDL_Cursor *g_arrowCursor = nullptr, *g_bulldozeCursor = nullptr;

static int g_mouseX = 0, g_mouseY = 0;

// What a fence drag or bulldozer click did: charge it, name new exhibits,
// ask before draining a tank
static void handleToolResult(const World::ToolResult &r, ResourceManager *rm) {
  if (r.cost > 0)
    g_sim.spend(ZooSim::Construction, r.cost);
  // A new exhibit: exhibit.wav, then its name asked
  if (!r.newExhibits.empty())
    Sound::get().play("sounds/exhibit", 100);
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
  if (!r.askMerge.empty())
    g_hud->askConfirm(r.askMerge, [rm] {
      if (int refund = g_world->confirmFenceRemoval())
        g_sim.earn(ZooSim::Recycling, refund);
      updateHudClock(rm);
    });
  if (r.askEscape)
    g_hud->askConfirm(rm->getString(157), [rm] {
      if (int refund = g_world->confirmFenceRemoval())
        g_sim.earn(ZooSim::Recycling, refund);
      updateHudClock(rm);
    });
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
  // Adopted: its price; clicked: its panel
  if (r.animalCost > 0)
    g_sim.spend(ZooSim::AnimalPurchase, r.animalCost);
  if (r.animal >= 0)
    g_hud->showAnimal(r.animal);
  // A building clicked: its Building Information
  if (r.building >= 0)
    g_hud->showBuilding(r.building);
  // A guest clicked: its panel; a woman's "hello" (vofem3, as the original)
  if (r.guest >= 0) {
    g_hud->showGuest(r.guest);
    if (const Guests::Guest *g = g_world->getGuests().guest(r.guest)) {
      const Guests::Type &t = g_world->getGuests().types()[g->type];
      Sound::get().play(t.female ? "guests/vofem3" : "guests/vomale1", 1000);
    }
  }
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

// The terraform account settled: paid for (Construction) if the zoo can,
// else put back as it was
static void settleTerrain(ResourceManager *rm) {
  TerrainTool &tt = g_world->getTerrainTool();
  float c = tt.cost();
  if (c > 0 && c > g_sim.cash()) {
    tt.undo();
    g_hud->showMessage(rm->getString(3363));
  } else if (int charge = tt.accept(); charge > 0) {
    g_sim.spend(ZooSim::Construction, charge);
  }
  g_world->getAnimals().markDirty();
  updateHudClock(rm);
}

// Clicks that landed on the interface (decided before the HUD handled them:
// a window's X closes it, and the click mustn't then reach the tool under it)
static std::vector<SDL_Point> g_uiClicks;

static void worldInputs(SDL_Renderer *renderer, ResourceManager *rm,
                        const std::vector<Input> &raw) {
  if (!g_hud || !g_world)
    return;
  // Undo greyed out with nothing to undo
  if (UiElement *undo = g_hud->getElementById(1075))
    undo->setDisabled(!g_world->canUndo());
  // The view toggles: foliage, buildings, guests hidden
  {
    PlacedObjects &po = g_world->objectsMutable();
    po.hideFoliage = !g_hud->viewShown(1066);
    po.hideBuildings = !g_hud->viewShown(1067);
    g_world->getGuests().hideAll = !g_hud->viewShown(1068);
  }
  UiTransform t = getUiTransform(renderer);
  if (g_widescreen)
    t.offsetX = t.offsetY = 0;
  // Guests: their admissions on the books, the zoo's counts and the HUD's
  // guest bar
  {
    Guests &gs = g_world->getGuests();
    g_world->setEconomy(static_cast<int>(g_sim.rating()) + g_hud->marketingBenefit(), g_hud->zooInfo().admission);
    int came = 0;
    double paid = gs.takeIncome(came);
    if (double given = gs.takeDonations(); given > 0) {
      g_sim.earn(ZooSim::Donations, given);
      updateHudClock(rm);
    }
    if (double sold = gs.takeConcessions(); sold > 0) {
      g_sim.earn(ZooSim::Concessions, sold);
      updateHudClock(rm);
    }
    if (came > 0) {
      g_sim.earn(ZooSim::AdmissionsIncome, paid);
      g_sim.count(ZooSim::Admissions, came);
      updateHudClock(rm);
    }
    for (int id : gs.takeMessages())
      g_hud->postMessage(rm->getString(id), id == 10021 || id == 10017 ? 1 : 0); // (good news: guests very happy, a good value)
    UiGameScreen::ZooInfo info = g_hud->zooInfo();
    int animals = static_cast<int>(g_world->getAnimals().members().size()), exhibits = 0;
    for (const Fences::Exhibit &e : g_world->getFences().exhibits())
      exhibits += e.named ? 1 : 0;
    int guests = static_cast<int>(gs.guests().size());
    // Attractions: objects that satisfy "fun"
    static std::map<std::string, bool> funOf;
    int attractions = 0;
    for (const PlacedObjects::Object &o : g_world->getObjects().objects()) {
      if (o.fence)
        continue;
      std::string file = PlacedObjects::fileOf(o);
      auto f = funOf.find(file);
      if (f == funOf.end()) {
        bool fun = false;
        int size = 0;
        if (void *data = rm->getFileBytes(file, &size)) {
          std::string text(static_cast<const char *>(data), static_cast<size_t>(size));
          free(data);
          std::string low = Utils::string_to_lower(text);
          size_t at = low.find("[satisfies]");
          if (at != std::string::npos) {
            size_t end = low.find('[', at + 1);
            fun = low.substr(at, end == std::string::npos ? std::string::npos : end - at).find("fun") != std::string::npos;
          }
        }
        f = funOf.emplace(file, fun).first;
      }
      attractions += f->second ? 1 : 0;
    }
    if (info.guestCount != guests || info.animalCount != animals || info.exhibitCount != exhibits ||
        info.attractionCount != attractions || info.benefactorCount != gs.members) {
      info.attractionCount = attractions;
      info.benefactorCount = gs.members;
      info.guestCount = guests;
      info.animalCount = animals;
      info.exhibitCount = exhibits;
      g_hud->setZooInfo(info);
    }
    if (auto *bar = dynamic_cast<UiStatusImage *>(g_hud->getElementById(1013)))
      bar->setValue(guests ? gs.averageHappiness() : 0);
  }
  // The zoo rating, every 4 s (zoo.exe 0x41f881 / 0x41fcc1), each term in
  // whole numbers: 15 x the healthy share of its animals; 10 x its kinds in
  // exhibits out of 44; (avg happiness + 100) x 25 / 200 for
  // animals and for guests; its value (cash + animals', objects', fences'
  // and path tiles' prices, 0-30000) / 3000; 5% of the research done
  // (cost-0 programs count; all of it with none to do); esthetics (0-10:
  // objects', fences' and path tiles' EstheticBonus over the tiles objects
  // stand on - a new Death Mountain zoo: 2322 / ~740 = 3, so 37, as
  // measured); less 50 while an animal is loose,
  // 2 less a day after (cEscapedAnimalChange / Time); 0-100
  {
    static float ratingClock = 0;
    static double lastEscape = -1e9;
    static Uint32 lastTick = SDL_GetTicks();
    Uint32 nowTick = SDL_GetTicks();
    ratingClock += g_world->isPaused() ? 0.0f : (nowTick - lastTick) / 1000.0f;
    lastTick = nowTick;
    if (ratingClock >= 4.0f) {
      ratingClock = 0;
      const Animals &an = g_world->getAnimals();
      int n = static_cast<int>(an.members().size());
      std::set<int> kinds;
      long sumH = 0;
      int sick = 0;
      long animalValue = 0;
      for (const Animals::Member &m : an.members()) {
        if (m.exhibit >= 0)
          kinds.insert(m.type);
        sumH += static_cast<long>(m.happiness);
        sick += m.sick ? 1 : 0;
        animalValue += an.types()[m.type].cost;
      }
      int r = 0;
      if (n > 0)
        r += 15 * (n - sick) / n;
      int s = static_cast<int>(kinds.size());
      if (s > 0)
        r += 10 * s / std::max(s, 44);
      int avgA = n ? static_cast<int>(sumH / n) : 0;
      r += (avgA + 100) * 25 / 200;
      const Guests &gs = g_world->getGuests();
      int avgG = 0;
      {
        long sum = 0;
        int k = 0;
        for (const Guests::Guest &gu : gs.guests())
          if (gu.x > -999) {
            sum += static_cast<long>(gu.happiness);
            k++;
          }
        avgG = k ? static_cast<int>(sum / k) : 0;
      }
      r += (avgG + 100) * 25 / 200;
      // Value: cash, the animals', every object's (the map's own too) and
      // every fence's price
      {
        static std::unordered_map<std::string, int> priceOf;
        if (priceOf.empty())
          for (const CatalogItem &item : ItemCatalog::get().all())
            priceOf[item.file] = item.cost;
        long v = static_cast<long>(g_sim.cash()) + animalValue;
        for (const PlacedObjects::Object &o : g_world->getObjects().objects()) {
          if (o.fence)
            continue;
          auto it = priceOf.find(PlacedObjects::fileOf(o));
          if (it != priceOf.end())
            v += it->second;
        }
        const Fences &fn = g_world->getFences();
        for (const auto &kv : fn.pieces())
          if (kv.second.type >= 0 && kv.second.type < static_cast<int>(fn.types().size()))
            v += fn.types()[kv.second.type].cost;
        // (each path tile is an object too)
        const WorldMap &wm = g_world->getMap();
        for (int y = 0; y < wm.getHeight(); y++)
          for (int x = 0; x < wm.getWidth(); x++) {
            int t = wm.getPathType(x, y);
            if (t < 0 || t >= static_cast<int>(wm.getPathTypes().size()))
              continue;
            auto it = priceOf.find("paths/" + wm.getPathTypes()[t] + ".ai");
            if (it != priceOf.end())
              v += it->second;
          }
        v = std::clamp(v, 0L, 30000L);
        r += static_cast<int>((v * 10 / 300) / 100);
      }
      // Research: the share of programs done, 5 points for all (100% with none)
      {
        int total = 0, done = 0;
        for (ResearchBranch &b : Research::get().branches())
          for (ResearchCategory &c : b.categories)
            for (ResearchProgram &pr : c.programs) {
              total++;
              // (one costing nothing is complete from the start: zoo.exe
              // 0x59104d, progress 0 >= cost 0)
              done += pr.done || pr.cost <= 0 ? 1 : 0;
            }
        int pct = total ? 100 * done / total : 100;
        r += pct * 5 / 100;
      }
      // Esthetics: every object's EstheticBonus to guests (man, woman, boy,
      // girl) and every fence's (a broken one -45 each, worn down -11 more),
      // over the tiles that have something on them; 0 to 10
      {
        static std::map<std::string, std::pair<int, int>> bonusOf; // file -> bonus, footprint tiles
        double bonus = 0;
        std::set<std::pair<int, int>> tiles;
        auto esthetic = [&](const std::string &file, int &sum, int &fx, int &fy) {
          sum = 0;
          fx = fy = 1;
          if (IniReader *ai = rm->getIniReader(file)) {
            std::vector<std::string> v = ai->getList("estheticbonus", "v");
            for (size_t k = 0; k + 1 < v.size(); k += 2) {
              int who = std::atoi(v[k].c_str());
              if (who >= 9503 && who <= 9506)
                sum += std::atoi(v[k + 1].c_str());
            }
            fx = std::max(1, ai->getInt("characteristics/integers", "cfootprintx", 1));
            fy = std::max(1, ai->getInt("characteristics/integers", "cfootprinty", 1));
            delete ai;
          }
        };
        for (const PlacedObjects::Object &o : g_world->getObjects().objects()) {
          if (o.fence)
            continue;
          std::string file = PlacedObjects::fileOf(o);
          auto b = bonusOf.find(file);
          if (b == bonusOf.end()) {
            int sum, fx, fy;
            esthetic(file, sum, fx, fy);
            b = bonusOf.emplace(file, std::make_pair(sum, fx * 100 + fy)).first;
          }
          bonus += b->second.first;
          int fx = b->second.second / 100, fy = b->second.second % 100;
          for (int j = 0; j < (fy + 1) / 2; j++)
            for (int k = 0; k < (fx + 1) / 2; k++)
              tiles.insert({static_cast<int>(o.x - fx / 4.0f) + k, static_cast<int>(o.y - fy / 4.0f) + j});
        }
        // (fences: zoo walls count nothing)
        const Fences &fn = g_world->getFences();
        for (const auto &kv : fn.pieces()) {
          if (kv.second.type < 0 || kv.second.type >= static_cast<int>(fn.types().size()))
            continue;
          const auto &ft = fn.types()[kv.second.type];
          if (ft.zooWall)
            continue;
          std::string file = "fences/" + ft.key + ".ai";
          auto b = bonusOf.find(file);
          if (b == bonusOf.end()) {
            int sum, fx, fy;
            esthetic(file, sum, fx, fy);
            b = bonusOf.emplace(file, std::make_pair(sum, 101)).first;
          }
          double v = b->second.first;
          float life = kv.second.life < 0 ? static_cast<float>(ft.life) : kv.second.life;
          if (life <= 0 && !kv.second.gate)
            v += -45.0 * 4;
          if (!ft.indestructible && life <= ft.decayedLife)
            v += -11.0 * 4;
          bonus += v;
        }
        // Paths: each tile's EstheticBonus (stone path 6 each to men, women,
        // boys, girls); they don't make a tile count as taken
        {
          const WorldMap &wm = g_world->getMap();
          for (int y = 0; y < wm.getHeight(); y++)
            for (int x = 0; x < wm.getWidth(); x++) {
              int t = wm.getPathType(x, y);
              if (t < 0 || t >= static_cast<int>(wm.getPathTypes().size()))
                continue;
              std::string file = "paths/" + wm.getPathTypes()[t] + ".ai";
              auto b = bonusOf.find(file);
              if (b == bonusOf.end()) {
                int sum, fx, fy;
                esthetic(file, sum, fx, fy);
                b = bonusOf.emplace(file, std::make_pair(sum, 101)).first;
              }
              bonus += b->second.first;
            }
        }
        double e = tiles.empty() ? 0 : bonus / tiles.size();
        g_esthetic = static_cast<int>(std::floor(std::clamp(e, 0.0, 10.0)));
        r += g_esthetic;
      }
      // Escaped: -50, 2 less each game day since one was last loose
      {
        double gameDays = g_world->getGuests().clock / ZooSim::kSecondsPerDay;
        if (an.escapedCount() > 0)
          lastEscape = gameDays;
        int days = static_cast<int>(std::floor(gameDays - lastEscape));
        r -= std::max(0, 50 - 2 * days);
      }
      int rating = std::clamp(r, 0, 100);
      g_sim.setRating(rating);
      if (auto *zoo = dynamic_cast<UiStatusImage *>(g_hud->getElementById(1015)))
        zoo->setValue(rating);
      if (auto *bar = dynamic_cast<UiStatusImage *>(g_hud->getElementById(1011)))
        bar->setValue(n ? static_cast<int>((sumH / n + 100) / 2) : 0);
      UiGameScreen::ZooInfo info = g_hud->zooInfo();
      if (info.rating != rating) {
        info.rating = rating;
        g_hud->setZooInfo(info);
      }
    }
  }
  for (const Animals::Notice &n : g_world->getAnimals().takeNotices())
    g_hud->postMessage(n.text, n.kind, n.animal >= 0 ? UiGameScreen::Subject::Animal : UiGameScreen::Subject::None,
                       n.animal);
  // Dung recycled through a compost (Recycling Benefit)
  if (double recycled = g_world->getStaff().takeRecycling(); recycled > 0) {
    g_sim.earn(ZooSim::Recycling, recycled);
    updateHudClock(rm);
  }
  // Food the keepers bought (Zoo Upkeep Cost)
  if (double food = g_world->getStaff().takeUpkeep(); food > 0) {
    g_sim.spend(ZooSim::Upkeep, food);
    updateHudClock(rm);
  }
  // The animals' news ("Plains Zebra 2 has escaped.": a notice with OK,
  // as the original)
  for (const std::string &m : g_world->getAnimals().takeMessages())
    g_hud->tell(m);
  // The terraform page's tool (Buy Habitat's last two tabs). Changing tab
  // or closing the panel settles the account: paid if the zoo can, else
  // undone ("There are not enough funds to perform this modification")
  UiGameScreen::TerraformState terraform =
      g_hud->bulldozerOn() ? UiGameScreen::TerraformState{} : g_hud->terraformState();
  {
    TerrainTool &tt = g_world->getTerrainTool();
    if (tt.isActive() && (!terraform.active || terraform.painting != tt.painting))
      settleTerrain(rm);
    g_world->setTerrainTool(terraform.active, terraform.painting, terraform.type, terraform.size,
                            terraform.mode);
    if (tt.isActive()) {
      float c = tt.cost();
      g_hud->setTerraformCost(c <= g_sim.cash() ? c : -c);
    }
    std::string sound = tt.takeSound();
    if (!sound.empty())
      Sound::get().play(sound);
  }
  // Gate mode (Buy Habitat's gate button)
  {
    bool gates = !g_hud->bulldozerOn() && !terraform.active && g_hud->gateMode();
    if (gates != g_world->getGateTool())
      g_world->setGateTool(gates);
    if (gates)
      g_hud->setWorldTip(rm->getString(3240)); // "Click for Manual Entrance Placement."
  }
  // The tool picked: a fence in Buy Habitat, or the bulldozer
  int tool = -1;
  std::string file = terraform.active ? std::string() : g_hud->fenceToolFile();
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
    std::string file = g_hud->bulldozerOn() || terraform.active ? "" : g_hud->pathToolFile();
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
  // Objects picked (shelters, toys, buildings, scenery, foliage, rocks) are
  // put down where they fit
  {
    int cost = 0, facing = 0;
    std::string file = g_hud->bulldozerOn() || terraform.active ? "" : g_hud->objectToolFile(cost, facing);
    g_world->setObjectTool(file, cost, facing);
  }
  // Animals picked in Adopt Animals (male or female) are put down in an
  // exhibit; zoo animals for now (the dinosaurs and marine animals later)
  {
    bool female = false;
    std::string file = g_hud->bulldozerOn() ? "" : g_hud->animalToolFile(female);
    Animals &an = g_world->getAnimals();
    int type = file.empty() ? -1 : an.typeOfFile(file);
    if (!an.isZooAnimal(type))
      type = -1;
    g_world->setAnimalTool(type, female);
  }
  // The Paths tab's walkway line
  g_hud->setWalkwayLabel(g_world->getBuildHeight() > 0
                             ? "Walkway: +" + std::to_string(g_world->getBuildHeight())
                             : std::string("Walkway: ground"));
  // The bulldozer's cursor while it's picked
  {
    SDL_Cursor *want = g_world->isBulldozing() && g_bulldozeCursor ? g_bulldozeCursor : g_arrowCursor;
    if (want && SDL_GetCursor() != want)
      SDL_SetCursor(want);
  }
  // The bulldozer over a piece: its name
  if (g_world->isBulldozing() && !g_world->bulldozeName().empty())
    g_hud->setWorldTip(g_world->bulldozeName());
  // No tool: a tank wall's hint, a diver platform's tank
  if (!g_world->hoverTip().empty())
    g_hud->setWorldTip(g_world->hoverTip());
  // The tank whose panel is open: an arrow over its platform
  g_world->getFences().selectedTank = g_hud->shownExhibitId();
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
    if (in.event == InputEvent::LEFT_CLICK)
      for (const SDL_Point &c : g_uiClicks)
        overUi = overUi || (c.x == in.x && c.y == in.y);
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
      g_hud->dropPicks();
      // (and the bulldozer put away: "hotkeys")
      if (Features::hotkeys)
        g_hud->bulldozerOff();
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
           "flat | terrainids | tiles <+|->   debug views: no heights, terrain ids, tile size\n"
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
  if (c == "flat") {
    g_world->getRenderer().toggleElevation();
    return g_world->getRenderer().isElevationEnabled() ? "Heights shown." : "Flat view (heights off).";
  }
  if (c == "terrainids") {
    g_world->getRenderer().toggleTerrainDebug();
    return "Terrain ids toggled.";
  }
  if (c == "tiles") {
    WorldRenderer &r = g_world->getRenderer();
    int step = w.size() > 1 && w[1] == "-" ? -1 : 1;
    r.setTileSize(std::clamp(r.getTileWidth() + 8 * step, 32, 256),
                  std::clamp(r.getTileHeight() + 4 * step, 16, 128));
    return "Tile size " + std::to_string(r.getTileWidth()) + "x" + std::to_string(r.getTileHeight());
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
                   g_world->getPathTool().empty() && !g_world->isAdjustingTank()))
    return;
  UiTransform t = getUiTransform(renderer);
  rm->setTextScale(t.scale);
  // The drag mode (or a path's build height), under where the price goes
  bool pathHeight = !g_world->getPathTool().empty() && Features::elevatedPaths;
  bool canRaise = World::raisable(g_world->getPathTool());
  if ((g_world->getFenceTool() >= 0 && Features::fenceModes) || pathHeight) {
    // A small panel by the cursor, in the game's colours: "Mode: ..." in
    // gold, the walkway height (paths), and "Tab: change mode"
    std::vector<std::pair<std::string, SDL_Color>> lines;
    const SDL_Color gold{255, 223, 41, 255}, white{235, 235, 235, 255}, grey{185, 190, 170, 255};
    bool modes = g_world->getFenceTool() >= 0 ? Features::fenceModes : Features::pathModes;
    if (modes)
      lines.push_back({std::string("Mode: ") + World::fenceModeName(g_world->getFenceTool() >= 0 ? g_world->getFenceMode()
                                                                                 : g_world->getPathMode()),
                       gold});
    if (pathHeight) {
      if (!canRaise)
        lines.push_back({"Ground only", white});
      else if (g_world->isLayingLine())
        lines.push_back({"Walkway +" + std::to_string(g_world->getBuildHeight()) + "   Click: build   Esc: stop", white});
      else if (g_world->getBuildHeight() > 0)
        lines.push_back({"Walkway +" + std::to_string(g_world->getBuildHeight()) + "   Click: start   Wheel: height", white});
      else
        lines.push_back({"Ground   Wheel: walkway", white});
    }
    if (modes)
      lines.push_back({"Tab: change mode", grey});
    std::vector<SDL_Texture *> tex;
    int boxW = 0, boxH = 0;
    for (size_t li = 0; li < lines.size(); li++) {
      const auto &[text, colour] = lines[li];
      // ("Mode:" in the dialogs' larger bold font)
      bool big = modes && li == 0;
      SDL_Texture *m = rm->getStringTexture(renderer, big ? 12005 : 4136, text, colour, big ? 12006 : 4137);
      tex.push_back(m);
      int w = 0, h = 0;
      if (m)
        rm->getTextSize(m, &w, &h);
      boxW = std::max(boxW, w);
      boxH += h;
    }
    int pad = static_cast<int>(5 * t.scale);
    SDL_Rect box = {g_mouseX + static_cast<int>(16 * t.scale), g_mouseY + static_cast<int>(12 * t.scale),
                    boxW + pad * 2, boxH + pad * 2};
    SDL_BlendMode was;
    SDL_GetRenderDrawBlendMode(renderer, &was);
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer, 34, 29, 15, 240);
    SDL_RenderFillRect(renderer, &box);
    SDL_SetRenderDrawColor(renderer, 255, 217, 90, 255);
    SDL_RenderDrawRect(renderer, &box);
    SDL_SetRenderDrawBlendMode(renderer, was);
    int y = box.y + pad;
    for (SDL_Texture *m : tex) {
      if (!m)
        continue;
      int w = 0, h = 0;
      rm->getTextSize(m, &w, &h);
      SDL_Rect r = {box.x + pad, y, w, h};
      SDL_RenderCopy(renderer, m, nullptr, &r);
      y += h;
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
  if (g_world->isBulldozing())
    r = {g_mouseX + static_cast<int>(6 * t.scale), g_mouseY + static_cast<int>(8 * t.scale), w, h};
  SDL_RenderCopy(renderer, text, nullptr, &r);
}

extern float g_ZoomLevel; // World.cpp
static UiAction hudInputs(SDL_Renderer *renderer, std::vector<Input> inputs);

// A frame's input in a game: the HUD first, then the map - but not a click
// that landed on the interface (decided before the HUD handled it: a
// window's X closes it, and the click mustn't then reach the tool held)
static UiAction gameInputs(SDL_Renderer *renderer, ResourceManager *rm, const std::vector<Input> &inputs) {
  bool dialogBefore = g_hud->hasDialog();
  g_uiClicks.clear();
  UiTransform ut = getUiTransform(renderer);
  for (const Input &in : inputs)
    if (in.type == InputType::POSITIONED && in.event == InputEvent::LEFT_CLICK &&
        g_hud->isOverHud(static_cast<int>((in.x - ut.offsetX) / ut.scale),
                         static_cast<int>((in.y - ut.offsetY) / ut.scale)))
      g_uiClicks.push_back({in.x, in.y});
  UiAction action = hudInputs(renderer, inputs);
  if (!dialogBefore && !g_hud->hasDialog())
    worldInputs(renderer, rm, inputs);
  return action;
}

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
    // A path that can be raised picked: the wheel sets the walkway height
    // (Ctrl + wheel still zooms)
    if (g_world && Features::elevatedPaths && !g_world->getPathTool().empty() &&
        World::raisable(g_world->getPathTool()) && !(SDL_GetModState() & KMOD_CTRL)) {
      g_world->adjustBuildHeight(in.event == InputEvent::SCROLL_UP ? 1 : -1);
      continue;
    }
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
static std::string g_currentMapPath; // the map the game was started on (saves name it)
static Goals g_goals;                 // its scenario goals (freeform: awards, donations)

// zoo.exe 0x532c75: screenshots/Zoo%03d.jpg (one past the highest there,
// Zoo001 first, back to Zoo000 after 999; JPEG quality 75, else a BMP),
// "Wrote screenshot in %s." (10507) or "Failed to write screenshot." (10508).
// (The folder sits beside the saved games.)
static void takeSnapshot(SDL_Renderer *renderer, ResourceManager *rm) {
  std::filesystem::path dir = std::filesystem::path(SaveGame::folder()).parent_path() / "screenshots";
  std::error_code ec;
  std::filesystem::create_directories(dir, ec);
  int highest = 0;
  for (const auto &e : std::filesystem::directory_iterator(dir, ec)) {
    std::string n = e.path().filename().string();
    if (n.size() >= 7 && (n.rfind("Zoo", 0) == 0 || n.rfind("zoo", 0) == 0))
      highest = std::max(highest, std::atoi(n.substr(3, 3).c_str()));
  }
  char name[32];
  std::snprintf(name, sizeof name, "Zoo%03d", (highest + 1) % 1000);
  int w = 0, h = 0;
  SDL_GetRendererOutputSize(renderer, &w, &h);
  SDL_Surface *s = SDL_CreateRGBSurfaceWithFormat(0, w, h, 24, SDL_PIXELFORMAT_RGB24);
  bool ok = false;
  std::string written;
  if (s && SDL_RenderReadPixels(renderer, nullptr, SDL_PIXELFORMAT_RGB24, s->pixels, s->pitch) == 0) {
    written = (dir / (std::string(name) + ".jpg")).string();
    ok = IMG_SaveJPG(s, written.c_str(), 75) == 0;
    if (!ok) {
      written = (dir / (std::string(name) + ".bmp")).string();
      ok = SDL_SaveBMP(s, written.c_str()) == 0;
    }
  }
  if (s)
    SDL_FreeSurface(s);
  if (!g_hud)
    return;
  if (ok) {
    std::string m = rm->getString(10507);
    size_t at = m.find("%s");
    if (at != std::string::npos)
      m.replace(at, 2, dir.string());
    g_hud->postMessage(m);
  } else {
    g_hud->postMessage(rm->getString(10508));
  }
}
static bool g_popupPaused = false;    // a goal's popup paused the game
static bool g_dialogPaused = false;   // a dialog (a name, a question) paused it

// The game paused or going: the clock, "The game is paused." and the Pause /
// Play button (they share a spot: the one that undoes the state shows)
static void setGamePaused(bool pause) {
  if (!g_world || !g_hud)
    return;
  g_world->setPaused(pause);
  g_hud->setPausedMessage(pause);
  if (UiElement *p = g_hud->getElementById(1071))
    p->setHidden(pause);
  if (UiElement *p = g_hud->getElementById(1072))
    p->setHidden(!pause);
}

// A goal's popup read: the game goes on (if the popup paused it)
static void resumeAfterPopup() {
  if (g_popupPaused && g_hud && !g_hud->dialogOpen()) {
    g_popupPaused = false;
    setGamePaused(false);
  }
}

// The goal engine's view of the zoo (zoo.exe 0x41d665) and what its
// triggers do
static void setupGoals(ResourceManager *rm) {
  Goals::Hooks h;
  h.measure = [](const Goals::Goal &g) -> int {
    if (!g_world)
      return 0;
    int months = static_cast<int>(g_sim.history().size()); // (1 in the first month)
    Animals &an = g_world->getAnimals();
    if (g.rulea == 1) {
      // (not before month arga)
      if (months < g.arga)
        return 0;
      switch (g.ruleb) {
      case 0: { // animals' average happiness, 0-100 (none: 50)
        double sum = 0;
        int n = 0;
        for (const Animals::Member &m : an.members()) {
          sum += m.happiness;
          n++;
        }
        int raw = n ? static_cast<int>(sum / n) : 0;
        return (raw + 100) * 100 / 200;
      }
      case 1: { // guests' average happiness, 0-100
        double sum = 0;
        int n = 0;
        for (const Guests::Guest &gu : g_world->getGuests().guests())
          if (gu.x > -999) {
            sum += gu.happiness;
            n++;
          }
        int raw = n ? static_cast<int>(sum / n) : 0;
        return (raw + 100) * 100 / 200;
      }
      case 2: return static_cast<int>(g_sim.rating());
      case 3: { // guests in the zoo now
        int n = 0;
        for (const Guests::Guest &gu : g_world->getGuests().guests())
          n += gu.x > -999 ? 1 : 0;
        return n;
      }
      case 4: return static_cast<int>(an.members().size());
      case 5: {
        int n = 0;
        for (const Animals::Member &m : an.members())
          n += m.sick ? 1 : 0;
        return n;
      }
      case 6: { // species in exhibits
        std::set<int> kinds;
        for (const Animals::Member &m : an.members())
          if (m.exhibit >= 0)
            kinds.insert(m.type);
        return static_cast<int>(kinds.size());
      }
      case 7: return static_cast<int>(g_sim.cash());
      case 11: return static_cast<int>(g_goals.awards.size());
      default: return 0;
      }
    }
    if (g.rulea == 2 && g.ruleb == 2) { // animals of a species (arga) and kind (argb: 0 m, 1 f, 3 young)
      int n = 0;
      for (const Animals::Member &m : an.members()) {
        if (an.types()[m.type].nameId != g.arga)
          continue;
        int kind = m.baby ? 3 : m.female ? 1 : 0;
        n += kind == g.argb ? 1 : 0;
      }
      return n;
    }
    if (g.rulea == 6 && g.ruleb == 0) { // at least arga exhibits whose animals average value or more
      int count = 0;
      for (const Fences::Exhibit &ex : g_world->getFences().exhibits()) {
        if (ex.animals <= 0)
          continue;
        count += an.exhibitSuitability(ex.id) >= g.value ? 1 : 0;
      }
      return count >= std::max(1, g.arga) ? g.value : 0;
    }
    if (g.rulea == 7 && g.ruleb == 2)
      return months;
    return 0;
  };
  h.popup = [](const std::string &image, const std::string &text, bool pause) {
    if (!g_hud)
      return;
    g_hud->popup(image, text);
    // (pause=1: the game waits until it's been read)
    if (pause && g_world && !g_world->isPaused()) {
      setGamePaused(true);
      g_popupPaused = true;
    }
  };
  h.disable = [](int element, bool off) {
    UiElement *e = g_hud ? g_hud->getElementById(element) : nullptr;
    if (!e || e->isDisabled() == off)
      return false;
    e->setDisabled(off);
    return true;
  };
  h.hide = [](int element, bool hidden) {
    if (UiElement *e = g_hud ? g_hud->getElementById(element) : nullptr)
      e->setHidden(hidden);
  };
  h.cash = [](int amount, int mode) {
    if (mode == 2)
      g_sim.setCash(amount);
    else if (mode == 1)
      g_sim.setCash(g_sim.cash() + amount);
    else
      g_sim.earn(ZooSim::Donations, amount);
  };
  h.message = [rm](int id) {
    if (g_hud)
      g_hud->postMessage(rm->getString(id), 1);
  };
  h.award = [](int) {
    if (g_hud)
      g_hud->setAwards(g_goals.awards);
  };
  g_goals.setHooks(h);
}

// ----------------------------------------------------------------------------
// Research finished: its effect (zoo.exe 0x58fbf0). 2 a characteristic of
// the target type (staff: keepers' cSlowRate; maintenance workers'
// cCleanTrashRadius, cFixFenceModifier; tour guides' cTourGuideBonus;
// animals: 0 cKeeperArrivesChange, 1 cFoodUnitValue, 2
// cReproductionChance, 3 cSickChance, 4 cTimeDeath (the living ones too),
// 5 cNumberAnimalsMax); 4 the same for every animal of an expansion
// (target < 100) or family; 5 chow's price. Changed by effectval3: 0 add,
// 1 set, 2 by a percentage of itself (rounded toward 0). (0 makes an item
// available - in freeform everything already is; 1 animal houses'
// collections, 6 marine tricks, 7 a research discount: not done here)
// ----------------------------------------------------------------------------
static std::map<std::string, int> g_foodPercent; // chow -> price change, cents formula state
static int researchChange(int field, int value, int mode) {
  switch (mode) {
  case 0: return field + value;
  case 1: return value;
  case 2: return field + field * value / 100;
  default: return field;
  }
}
static void applyResearch(const ResearchProgram &p) {
  if (!g_world)
    return;
  int which = p.effectVal[0], value = p.effectVal[1], mode = p.effectVal[2];
  Animals &an = g_world->getAnimals();
  auto animalChange = [&](Animals::Type &t) {
    switch (which) {
    case 0: t.keeperArrivesChange = researchChange(t.keeperArrivesChange, value, mode); break;
    case 2: t.reproductionChance = researchChange(t.reproductionChance, value, mode); break;
    case 3: t.sickChance = researchChange(t.sickChance, value, mode); break;
    case 4: {
      t.timeDeath = researchChange(t.timeDeath, value, mode);
      int ti = static_cast<int>(&t - an.mutableTypes().data());
      for (Animals::Member &m : an.mutableMembers())
        if (m.type == ti && (mode != 2 || m.life > 0))
          m.life = researchChange(m.life, value, mode);
      break;
    }
    case 5: t.numberMax = researchChange(t.numberMax, value, mode); break;
    case 1: t.foodUnitValue = researchChange(t.foodUnitValue, value, mode); break;
    default: break;
    }
  };
  if (p.effect == 2) {
    for (Staff::Type &t : g_world->getStaff().mutableTypes()) {
      if (t.nameId != p.target)
        continue;
      if (t.kind == Staff::Kind::Guide && which == 0)
        t.tourBonus = researchChange(t.tourBonus, value, mode);
      else if (t.kind == Staff::Kind::Maint && which == 0)
        t.cleanTrashRadius = researchChange(t.cleanTrashRadius, value, mode);
      else if (t.kind == Staff::Kind::Maint && which == 1)
        t.fixFenceModifier = researchChange(t.fixFenceModifier, value, mode);
      else if ((t.kind == Staff::Kind::Keeper || t.kind == Staff::Kind::Scientist || t.kind == Staff::Kind::Trainer) &&
               which == 0) {
        t.slowRate = researchChange(t.slowRate, value, mode);
        t.speed = t.slowRate / 60.0f;
      }
      // (keepers' index 1 changes nothing outside the scientists: zoo.exe)
    }
    for (Animals::Type &t : an.mutableTypes())
      if (t.nameId == p.target)
        animalChange(t);
  } else if (p.effect == 4) {
    for (Animals::Type &t : an.mutableTypes())
      if (p.target < 100 ? t.expansion == p.target : t.family == p.target)
        animalChange(t);
  } else if (p.effect == 5 && which == 0) {
    // (every chow; its price in cents, changed, back to dollars)
    g_foodPercent[""] = researchChange(g_foodPercent.count("") ? g_foodPercent[""] : 10000, value, mode);
  }
}

// What research changes, as the files had it: put back at a new game, then
// what's done applied (costing nothing: done from the start)
static void resetResearchEffects() {
  if (!g_world)
    return;
  struct StaffBase { int slowRate, tourBonus, cleanTrashRadius, fixFenceModifier; };
  struct AnimalBase { int keeperArrivesChange, reproductionChance, sickChance, timeDeath, numberMax, foodUnitValue; };
  static std::vector<StaffBase> staffBase;
  static std::vector<AnimalBase> animalBase;
  auto &st = g_world->getStaff().mutableTypes();
  auto &an = g_world->getAnimals().mutableTypes();
  if (staffBase.size() != st.size()) {
    staffBase.clear();
    for (const Staff::Type &t : st)
      staffBase.push_back({t.slowRate, t.tourBonus, t.cleanTrashRadius, t.fixFenceModifier});
  }
  if (animalBase.size() != an.size()) {
    animalBase.clear();
    for (const Animals::Type &t : an)
      animalBase.push_back({t.keeperArrivesChange, t.reproductionChance, t.sickChance, t.timeDeath, t.numberMax, t.foodUnitValue});
  }
  for (size_t i = 0; i < st.size(); i++) {
    st[i].slowRate = staffBase[i].slowRate;
    st[i].speed = st[i].slowRate / 60.0f;
    st[i].tourBonus = staffBase[i].tourBonus;
    st[i].cleanTrashRadius = staffBase[i].cleanTrashRadius;
    st[i].fixFenceModifier = staffBase[i].fixFenceModifier;
  }
  for (size_t i = 0; i < an.size(); i++) {
    an[i].keeperArrivesChange = animalBase[i].keeperArrivesChange;
    an[i].reproductionChance = animalBase[i].reproductionChance;
    an[i].sickChance = animalBase[i].sickChance;
    an[i].timeDeath = animalBase[i].timeDeath;
    an[i].numberMax = animalBase[i].numberMax;
    an[i].foodUnitValue = animalBase[i].foodUnitValue;
  }
  g_foodPercent.clear();
  for (ResearchBranch &b : Research::get().branches())
    for (ResearchCategory &c : b.categories)
      for (ResearchProgram &p : c.programs)
        if (p.done || p.cost <= 0)
          applyResearch(p);
}

// A map's goals, started as it is
static void startGoals(ResourceManager *rm, const std::string &mapPath) {
  SaveGame::goals = &g_goals;
  setupGoals(rm);
  Research::get().restart();
  resetResearchEffects();
  g_world->getStaff().foodPrice = [](const std::string &, double price) {
    auto it = g_foodPercent.find("");
    if (it == g_foodPercent.end())
      return price;
    // (the price as cents, changed as research said: 10000 = unchanged)
    long cents = static_cast<long>(price * 100);
    return static_cast<long>(cents * static_cast<double>(it->second) / 10000.0) * 0.01;
  };
  g_goals.load(rm, mapPath);
  if (g_hud)
    g_hud->setAwards(g_goals.awards);
}

// Save Game: the Windows "Save a zoo..." dialog (as the original), or the
// file given (tests); the zoo written there
static bool gameSave(const std::string &given = "") {
  if (!g_hud || !g_world)
    return false;
  std::string path = given.empty() ? SaveGame::askSavePath() : given;
  if (path.empty())
    return false; // (cancelled)
  bool ok = SaveGame::save(path, *g_world, g_sim, *g_hud, g_currentMapPath);
  SDL_Log("[Save] %s %s", ok ? "saved" : "couldn't save", path.c_str());
  g_hud->showMessage(ok ? "Zoo saved." : "The zoo couldn't be saved.",
                     ok ? SDL_Color{255, 255, 255, 255} : SDL_Color{255, 40, 40, 255});
  return ok;
}

// Load Game: "Load a zoo..." (or the file given): its map started afresh
// with a new screen, then the save put back over it
static bool gameLoad(ResourceManager *rm, const std::string &given = "") {
  if (!g_world)
    return false;
  std::string path = given.empty() ? SaveGame::askLoadPath() : given;
  if (path.empty())
    return false; // (cancelled)
  std::string mapPath = SaveGame::mapOf(path);
  // (a scenario's save: its scenario started afresh, else a freeform map)
  const std::string scn = "scenario:";
  bool scenario = mapPath.rfind(scn, 0) == 0;
  bool started = !mapPath.empty() && (scenario ? g_world->loadScenario(mapPath.substr(scn.size()))
                                               : g_world->loadFreeform(mapPath));
  if (!started) {
    SDL_Log("[Save] not a saved zoo: %s (map '%s')", path.c_str(), mapPath.c_str());
    if (g_hud)
      g_hud->showMessage("That isn't a saved zoo.", SDL_Color{255, 40, 40, 255});
    return false;
  }
  destroyHud();
  createHud(rm, g_world, g_currentStartingCash);
  g_currentMapPath = mapPath;
  if (!scenario)
    startGoals(rm, mapPath);
  bool ok = SaveGame::load(path, *g_world, g_sim, *g_hud);
  resetResearchEffects();
  updateHudClock(rm);
  SDL_Log("[Save] %s %s", ok ? "loaded" : "couldn't read", path.c_str());
  g_hud->showMessage(ok ? "Zoo loaded." : "The saved zoo couldn't be read.",
                     ok ? SDL_Color{255, 255, 255, 255} : SDL_Color{255, 40, 40, 255});
  return ok;
}
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
  world->edgeScroll = false;
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
  g_currentMapPath = chosen->path;
  // ZT_FLAT_LAB=1: everything inside the zoo made one level of plain grass,
  // the trees, rocks, paths and water gone (the entrance and the path by it
  // kept) - a flat place to test on
  if (std::getenv("ZT_FLAT_LAB")) {
    WorldMap &map = world->getMap();
    Fences &fn = world->getFences();
    const PlacedObjects::Object *gate = nullptr;
    for (const PlacedObjects::Object &o : world->getObjects().objects())
      if (o.className == "building" && o.typeName.size() >= 4 &&
          o.typeName.compare(o.typeName.size() - 4, 4, "gate") == 0)
        gate = &o;
    float gx = gate ? gate->x : map.getWidth() / 2.0f, gy = gate ? gate->y : map.getHeight() / 2.0f;
    const MapTile *gt = map.getTile(static_cast<int>(gx), static_cast<int>(gy));
    int level = gt ? gt->cornerHeight[0] : 4;
    auto nearGate = [&](float x, float y) { return std::fabs(x - gx) < 4.0f && std::fabs(y - gy) < 4.0f; };
    int tiles = 0;
    for (int y = 0; y < map.getHeight(); y++)
      for (int x = 0; x < map.getWidth(); x++) {
        if (!fn.insideZoo(x, y) || nearGate(x + 0.5f, y + 0.5f))
          continue;
        MapTile *t = map.getTileMutable(x, y);
        for (int c = 0; c < 4; c++)
          t->cornerHeight[c] = level;
        t->height = level;
        t->terrainType = 0; // grass
        map.setPath(x, y, "");
        tiles++;
      }
    map.touch();
    auto &objs = world->objectsMutable();
    int removed = 0;
    for (int i = static_cast<int>(world->getObjects().objects().size()) - 1; i >= 0; i--) {
      const PlacedObjects::Object &o = world->getObjects().objects()[i];
      if (o.fence || nearGate(o.x, o.y) || !fn.insideZoo(static_cast<int>(o.x), static_cast<int>(o.y)))
        continue;
      objs.remove(i);
      removed++;
    }
    world->reindex();
    SDL_Log("[FlatLab] %d tiles flat at %d, %d objects cleared", tiles, level, removed);
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
      drawToolCost(renderer, rm);
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
  // ZT_VIEW_SAVE=<file>: a saved zoo loaded and shown as it was saved (the
  // view it was left at, then the other three turns), its tanks and
  // animals listed, then quit - for looking at a player's save
  if (const char *view = std::getenv("ZT_VIEW_SAVE")) {
    bool ok = gameLoad(rm, view);
    note((std::string("loaded save ") + view + (ok ? " ok" : " FAILED")).c_str());
    if (!ok)
      return 1;
    world->getAnimals().takeMessages();
    g_hud->showPanel(0, "");
    for (const Fences::Exhibit &e : world->getFences().exhibits())
      SDL_Log("[ViewSave] exhibit %d '%s' tank %d tiles %zu wallSub %d floor %d ground %d water %.2f", e.id,
              e.name.c_str(), static_cast<int>(e.tank), e.tiles.size(), e.wallSub, e.floorHeight, e.groundHeight,
              e.water);
    for (const Animals::Member &m : world->getAnimals().members())
      SDL_Log("[ViewSave] animal %s at %.1f,%.1f exhibit %d escaped %d", m.name.c_str(), m.x, m.y, m.exhibit,
              static_cast<int>(m.escaped));
    capture("save_view");
    for (int pass = 0; pass < (std::getenv("ZT_VIEW_TWICE") ? 2 : 1); pass++) {
      if (pass == 1) {
        gameLoad(rm, view);
        world->getAnimals().takeMessages();
      }
      // (how long a frame takes to draw here)
      Uint64 f0 = SDL_GetPerformanceCounter();
      for (int i = 0; i < 20; i++) {
        SDL_RenderClear(renderer);
        world->draw(renderer);
        SDL_RenderPresent(renderer);
      }
      double each = (SDL_GetPerformanceCounter() - f0) * 1000.0 / SDL_GetPerformanceFrequency() / 20.0;
      SDL_Log("[ViewSave] counts: objects %zu guests %zu fences %zu exhibits %zu entities %zu",
              world->getObjects().objects().size(), world->getGuests().guests().size(),
              world->getFences().pieces().size(), world->getFences().exhibits().size(),
              world->getEntityManager().getAllEntities().size());
      std::vector<Fences::Drawable> art;
      world->getFences().collect(world->getRenderer(), world->getMap(), art);
      SDL_Log("[ViewSave] frame %.1f ms, fence drawables %zu", each, art.size());
    }
    for (int r = 1; r < 4; r++) {
      world->rotateView(1);
      capture(("save_view_rot" + std::to_string(r)).c_str());
    }
    return 0;
  }
  // A new zoo's rating, worked out as the original does: 37 (measured)
  {
    std::vector<Input> none;
    worldInputs(renderer, rm, none);
    SDL_Delay(4100);
    worldInputs(renderer, rm, none);
    check(("a new zoo rates " + std::to_string(static_cast<int>(g_sim.rating())) + " (the original 37)").c_str(),
          static_cast<int>(g_sim.rating()) == 37);
  }

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
  // (its benefit, +5, goes on the rating that picks how often guests come)
  check(("marketing min: +" + std::to_string(g_hud->marketingBenefit()) + " to the arrival rating").c_str(),
        g_hud->marketingBenefit() == 5);
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
    // ZT_TANK_LAB=<typeA>,<typeB>: two 4x4 tanks side by side (sharing a
    // wall) on clear ground, from all four sides, then quit (for comparing
    // tanks with the original quickly)
    if (const char *lab = std::getenv("ZT_TANK_LAB")) {
      for (const FenceType &ft : fences.types())
        if (ft.tank) {
          std::string sizes;
          for (int l = 0; l < 3; l++)
            if (Animation *a = ft.tankArt[1][l]) {
              int w = 0, h = 0, ax = 0, ay = 0;
              a->queryTexture(CompassDirection::NE, &w, &h);
              a->anchorOffset(CompassDirection::NE, &ax, &ay);
              sizes += " [" + std::to_string(l) + "] " + std::to_string(w) + "x" + std::to_string(h) + " anchor " +
                       std::to_string(ax) + "," + std::to_string(ay);
            }
          if (Animation *a = ft.tankLow[1]) {
            int w = 0, h = 0;
            a->queryTexture(CompassDirection::NE, &w, &h);
            sizes += " low " + std::to_string(w) + "x" + std::to_string(h);
          }
          SDL_Log("[TankLab] type %s (%s)%s", ft.key.c_str(), ft.name.c_str(), sizes.c_str());
        }
      // The pieces themselves, each type a row: [left, mid, right, column] x
      // [bottom, middle, top], then the low ones; NE then SE
      {
        SDL_Texture *t = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_TARGET, 2800, 760);
        SDL_SetRenderTarget(renderer, t);
        SDL_SetRenderDrawColor(renderer, 90, 140, 60, 255);
        SDL_RenderClear(renderer);
        int row = 0;
        for (const FenceType &ft : fences.types()) {
          if (!ft.tank)
            continue;
          int col = 0;
          for (CompassDirection dir : {CompassDirection::NE, CompassDirection::SE, CompassDirection::SW, CompassDirection::NW}) {
            for (int c = 0; c < 4; c++)
              for (int l = 0; l < 3; l++, col++)
                if (Animation *a = ft.tankArt[c][l]) {
                  float x = 30.0f + col * 48.0f, y = 60.0f + row * 90.0f;
                  SDL_SetRenderDrawColor(renderer, 255, 0, 0, 255);
                  SDL_RenderDrawPointF(renderer, x, y);
                  a->drawAnchored(renderer, x, y, dir, nullptr, 0);
                }
            for (Animation *a : {ft.ladder, ft.ladderOut, ft.platform}) {
              if (a) {
                float x = 30.0f + col * 48.0f, y = 60.0f + row * 90.0f;
                a->drawAnchored(renderer, x, y, dir, nullptr, 0);
                SDL_SetRenderDrawColor(renderer, 255, 0, 0, 255);
                SDL_RenderDrawPointF(renderer, x, y);
              }
              col++;
            }
            for (int c = 0; c < 3; c++, col++)
              if (Animation *a = ft.tankLow[c])
                a->drawAnchored(renderer, 30.0f + col * 48.0f, 60.0f + row * 90.0f, dir, nullptr, 0);
            col++;
          }
          row++;
        }
        {
          int col = 0;
          for (const char *part : {"top", "topn", "tope", "tops", "topw", "front", "right"}) {
            if (Animation *a = rm->getAnimation(std::string("water/salt/") + part + "/" + part)) {
              float x = 60.0f + col * 110.0f, y = 700.0f;
              a->drawAnchored(renderer, x, y, CompassDirection::N, nullptr, 0);
              SDL_SetRenderDrawColor(renderer, 255, 0, 0, 255);
              SDL_RenderDrawPointF(renderer, x, y);
              int w = 0, h = 0, ox = 0, oy = 0;
              a->queryTexture(CompassDirection::N, &w, &h);
              a->anchorOffset(CompassDirection::N, &ox, &oy);
              SDL_Log("[TankLab] water %s %dx%d anchor off %d,%d frames %d", part, w, h, ox, oy, a->frameCount());
            }
            col++;
          }
        }
        SDL_Surface *sf = SDL_CreateRGBSurfaceWithFormat(0, 2800, 760, 24, SDL_PIXELFORMAT_RGB24);
        SDL_RenderReadPixels(renderer, nullptr, SDL_PIXELFORMAT_RGB24, sf->pixels, sf->pitch);
        SDL_SaveBMP(sf, (outDir + "/tankpieces.bmp").c_str());
        SDL_FreeSurface(sf);
        SDL_SetRenderTarget(renderer, nullptr);
        SDL_DestroyTexture(t);
      }
      // The water's parts alone, on dark ground: salt, then fresh
      {
        SDL_Texture *t = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_TARGET, 760, 200);
        SDL_SetRenderTarget(renderer, t);
        SDL_SetRenderDrawColor(renderer, 40, 40, 40, 255);
        SDL_RenderClear(renderer);
        int row = 0;
        for (const char *set : {"salt", "glass"}) {
          int col = 0;
          for (const char *part : std::string(set) == "glass" ? std::vector<const char *>{"bgrey", "fgrey", "lgrey", "rgrey"}
                                                               : std::vector<const char *>{"top", "topn", "tope", "tops", "topw", "front", "right"}) {
            if (Animation *a = rm->getAnimation(std::string("water/") + set + "/" + part + "/" + part)) {
              for (int f = 0; f < std::max(1, a->frameCount()) && f < 1; f++)
                a->drawAnchored(renderer, 50.0f + col * 100.0f, 50.0f + row * 100.0f, CompassDirection::N, nullptr, 0);
              SDL_Log("[TankLab] water %s/%s frames %d", set, part, a->frameCount());
            }
            col++;
          }
          row++;
        }
        SDL_Surface *sf = SDL_CreateRGBSurfaceWithFormat(0, 760, 200, 24, SDL_PIXELFORMAT_RGB24);
        SDL_RenderReadPixels(renderer, nullptr, SDL_PIXELFORMAT_RGB24, sf->pixels, sf->pitch);
        SDL_SaveBMP(sf, (outDir + "/waterparts.bmp").c_str());
        SDL_FreeSurface(sf);
        SDL_SetRenderTarget(renderer, nullptr);
        SDL_DestroyTexture(t);
      }
      std::string spec = lab, ka = spec.substr(0, spec.find(',')),
                  kb = spec.find(',') == std::string::npos ? ka : spec.substr(spec.find(',') + 1);
      // (a patch by the chain-link pen made flat and clear: 9 x 5 tiles
      // with no fences or water)
      int lx = -1, ly = -1;
      {
        WorldMap &map = world->getMap();
        for (int r = 4; r < 40 && lx < 0; r++)
          for (int dy = -r; dy <= r && lx < 0; dy++)
            for (int dx = -r; dx <= r && lx < 0; dx++) {
              int x0 = cx + dx, y0 = cy + dy;
              bool ok = true;
              for (int y = y0 - 1; y <= y0 + 5 && ok; y++)
                for (int x = x0 - 1; x <= x0 + 9 && ok; x++) {
                  const MapTile *t = map.getTile(x, y);
                  ok = t && t->terrainType != 9 && t->terrainType != 10 && !map.isPath(x, y) &&
                       fences.insideZoo(x, y) && fences.exhibitAt(x, y) < 0 &&
                       !fences.at({true, x, y}) && !fences.at({false, x, y});
                }
              if (ok) {
                lx = x0;
                ly = y0;
              }
            }
        if (lx >= 0) {
          int h = map.getTile(lx, ly)->cornerHeight[0];
          for (int y = ly - 1; y <= ly + 5; y++)
            for (int x = lx - 1; x <= lx + 9; x++)
              if (MapTile *t = map.getTileMutable(x, y))
                for (int c = 0; c < 4; c++)
                  t->cornerHeight[c] = h;
          map.touch();
          auto &objs = world->objectsMutable();
          for (int i = static_cast<int>(world->getObjects().objects().size()) - 1; i >= 0; i--) {
            const PlacedObjects::Object &o = world->getObjects().objects()[i];
            if (o.x > lx - 3 && o.x < lx + 11 && o.y > ly - 3 && o.y < ly + 7)
              objs.remove(i);
          }
          world->reindex();
        }
      }
      SDL_Log("[TankLab] spot %d,%d: %s, %s", lx, ly, ka.c_str(), kb.c_str());
      if (lx >= 0) {
        lay(ka.c_str(), {{lx, ly}, {lx + 4, ly}, {lx + 4, ly + 4}, {lx, ly + 4}, {lx, ly}});
        enter();
        if (std::getenv("ZT_TANK_LAB_GAP"))
          lay(kb.c_str(), {{lx + 5, ly}, {lx + 9, ly}, {lx + 9, ly + 4}, {lx + 5, ly + 4}, {lx + 5, ly}});
        else
          lay(kb.c_str(), {{lx + 4, ly}, {lx + 8, ly}, {lx + 8, ly + 4}, {lx + 4, ly + 4}});
        enter();
        for (int i = 0; i < 40; i++)
          world->getFences().update(1.0f);
        world->centreOn(lx + 2.0f, ly + 2.0f);
        for (int r = 0; r < 4; r++) {
          capture(("tanklab_rot" + std::to_string(r)).c_str());
          world->rotateView(1);
        }
        // Dragged down by the mouse on its front wall, as a player does
        for (const Fences::Exhibit &x : fences.exhibits())
          if (x.tank && x.tiles.count({lx, ly})) {
            int bestY = -1, px = 0, py = 0;
            float mx = 0, my = 0;
            for (const auto &[e, pc] : fences.pieces()) {
              if (pc.tank != x.id && pc.tank2 != x.id)
                continue;
              float ex = e.alongX ? e.x + 0.5f : static_cast<float>(e.x);
              float ey = e.alongX ? static_cast<float>(e.y) : e.y + 0.5f;
              int qx, qy;
              world->pointToWindow(ex, ey, x.wallTop() - 0.25f, qx, qy);
              if (qy > bestY) {
                bestY = qy;
                px = qx;
                py = qy;
                mx = ex;
                my = ey;
              }
            }
            int ax, ay, bx, by;
            world->pointToWindow(mx, my, 0.0f, ax, ay);
            world->pointToWindow(mx, my, 1.0f, bx, by);
            int unit = ay - by;
            int before = x.wallSub;
            world->mouseMove(px, py);
            handleToolResult(world->mouseDown(px, py), rm);
            world->mouseMove(px, py + 2 * unit + unit / 4);
            handleToolResult(world->mouseUp(px, py + 2 * unit + unit / 4), rm);
            for (int i = 0; i < 20; i++)
              world->getFences().update(1.0f);
            SDL_Log("[TankLab] mouse drag down: wall %d -> %d over the ground (unit %d)", before,
                    x.wallSub, unit);
            capture("tanklab_mousedown");
            world->getFences().adjustWall(x.id, before - x.wallSub);
          }
        // Flush (walls down to the ground), then raised two steps: both tanks
        // ZT_TANK_LAB_ROT: each height from all four sides (default, flush,
        // two under, raised eight)
        if (std::getenv("ZT_TANK_LAB_ROT")) {
          int cur = 0;
          const char *names[] = {"def", "flush", "under", "high"};
          int want[] = {0, -1, -3, 8};
          for (int k = 0; k < 4; k++) {
            for (const Fences::Exhibit &x : fences.exhibits())
              if (x.tank && (x.tiles.count({lx, ly}) || x.tiles.count({lx + 6, ly + 1})))
                world->getFences().adjustWall(x.id, want[k] - cur);
            cur = want[k];
            for (int i = 0; i < 20; i++)
              world->getFences().update(1.0f);
            world->centreOn(lx + 2.0f, ly + 2.0f);
            for (int r = 0; r < 4; r++) {
              capture((std::string("tankrot_") + names[k] + std::to_string(r)).c_str());
              world->rotateView(1);
            }
          }
          // Raised, the water rising from near the floor (as after the
          // walls go up)
          for (const Fences::Exhibit &x : fences.exhibits())
            if (x.tank && (x.tiles.count({lx, ly}) || x.tiles.count({lx + 6, ly + 1})))
              const_cast<Fences::Exhibit &>(x).level = x.floorHeight + 0.6f;
          for (int f = 0; f < 4; f++) {
            for (int i = 0; i < 7; i++)
              world->getFences().update(0.25f);
            capture(("tankfill" + std::to_string(f)).c_str());
          }
          return 0;
        }
        int shot = 0;
        for (int step : {0, -1, -1, -1}) {
          int up = 0;
          for (const Fences::Exhibit &x : fences.exhibits())
            if (x.tank && (x.tiles.count({lx, ly}) || x.tiles.count({lx + 6, ly + 1}))) {
              world->getFences().adjustWall(x.id, step);
              up = static_cast<int>(std::lround((x.wallTop() - x.groundHeight) * 2));
            }
          for (int i = 0; i < 20; i++)
            world->getFences().update(1.0f);
          (void)up;
          capture(("tanklab_step" + std::to_string(shot++)).c_str());
        }
      }
      return 0;
    }
    // As the original: the tank as made, close up; its wall dragged up
    // three steps with no tool (the price beside the cursor), the water
    // following; its diver platform lit and named; dragged back down
    {
      const Fences::Exhibit *mt = nullptr;
      for (const Fences::Exhibit &x : fences.exhibits())
        if (x.tank)
          mt = &x;
      check("a tank made", mt != nullptr);
      if (mt) {
        world->centreOn(tx + 1.0f, ty + 1.0f);
        world->mouseMove(0, 0);
        world->getFences().update(5.0f);
        capture("tank_made");
        for (const auto &kv : fences.pieces())
          if (kv.second.tank == mt->id && kv.second.gate)
            SDL_Log("[PanelShots] tank gate %s %d,%d", kv.first.alongX ? "x" : "y", kv.first.x, kv.first.y);
        // The front wall lowest on the screen (not the gate), at its middle
        int px = 0, py = -1, unit = 0;
        float mx = 0, my = 0;
        for (const auto &kv : fences.pieces()) {
          if (kv.second.tank != mt->id || kv.second.gate)
            continue;
          const Fences::Edge &e = kv.first;
          float ex = e.alongX ? e.x + 0.5f : static_cast<float>(e.x);
          float ey = e.alongX ? static_cast<float>(e.y) : e.y + 0.5f;
          int qx, qy;
          world->pointToWindow(ex, ey, mt->wallTop() - 0.25f, qx, qy);
          if (qy > py) {
            px = qx;
            py = qy;
            mx = ex;
            my = ey;
          }
        }
        {
          int ax, ay, bx, by;
          world->pointToWindow(mx, my, 0.0f, ax, ay);
          world->pointToWindow(mx, my, 1.0f, bx, by);
          unit = ay - by;
        }
        world->mouseMove(px, py);
        check(("a tank wall's hint: " + world->hoverTip()).c_str(), world->hoverTip() == rm->getString(31079));
        g_hud->setWorldTip(world->hoverTip());
        g_mouseX = px;
        g_mouseY = py;
        g_hud->setMouse(px, py, SDL_GetTicks() - 1000);
        capture("tank_wall_hint");
        g_hud->setMouse(-1, -1, 0);
        g_hud->setWorldTip("");
        const int step = unit / 2; // (a wall step: a subtile, half a height unit)
        int top0 = mt->wallSub;
        double cash0 = g_sim.cash();
        handleToolResult(world->mouseDown(px, py), rm);
        world->mouseMove(px, py - 3 * step - step / 4);
        g_mouseX = px;
        g_mouseY = py - 3 * step - step / 4;
        check(("dragging shows the price ($" + std::to_string(world->hoverCost()) + ")").c_str(),
              world->hoverCost() == 3 * static_cast<int>(std::lround(fences.wallStepCost(mt->id))));
        capture("tank_wall_drag");
        handleToolResult(world->mouseUp(px, py - 3 * step - step / 4), rm);
        check(("a tank wall dragged up three steps (" + std::to_string(mt->wallSub - top0) + ", $" +
               std::to_string(std::lround(cash0 - g_sim.cash())) + ")").c_str(),
              mt->wallSub == top0 + 3 &&
                  std::lround(cash0 - g_sim.cash()) == 3 * std::lround(fences.wallStepCost(mt->id)));
        world->getFences().update(0.5f);
        capture("tank_water_rising");
        world->getFences().update(5.0f);
        check("the water follows the walls up", std::fabs(mt->level - (mt->wallTop() - 0.5f)) < 0.01f);
        // Waves now and then (one per 40000 / tiles ms), gone after 4 s
        {
          auto &mx = const_cast<Fences::Exhibit &>(*mt);
          mx.waves.clear();
          mx.waveIn = 0;
          size_t most = 0;
          for (int i = 0; i < 40; i++) {
            world->getFences().update(0.25f);
            most = std::max(most, mx.waves.size());
          }
          bool aged = true;
          for (const Fences::Exhibit::Wave &w : mx.waves)
            aged = aged && w.age < 4000.0f;
          capture("tank_waves");
          check(("waves on a clean tank (up to " + std::to_string(most) + " at once)").c_str(), most >= 1 && aged);
        }
        capture("tank_raised");
        // Raised: its hint anywhere on the wall's face, not only at its top
        {
          int fx, fy;
          world->pointToWindow(mx, my, (mt->groundHeight + mt->wallTop()) * 0.5f, fx, fy);
          world->mouseMove(fx, fy);
          check("a raised tank wall's hint half way down its face", world->hoverTip() == rm->getString(31079));
        }
        // The diver platform: lit, its tank named; clicking opens the tank
        for (const auto &kv : fences.pieces())
          if (kv.second.tank == mt->id && kv.second.gate) {
            const Fences::Edge &e = kv.first;
            float ex = e.alongX ? e.x + 0.5f : static_cast<float>(e.x);
            float ey = e.alongX ? static_cast<float>(e.y) : e.y + 0.5f;
            int gx, gy;
            world->pointToWindow(ex, ey, mt->wallTop() - 1.0f, gx, gy);
            gy -= unit;
            world->mouseMove(gx, gy);
            check(("the platform lit and named (" + world->hoverTip() + ")").c_str(),
                  fences.hoverPlatform == mt->id && world->hoverTip() == mt->name);
            g_hud->setWorldTip(world->hoverTip());
            g_hud->setMouse(gx, gy, SDL_GetTicks() - 1000);
            capture("tank_platform_hover");
            g_hud->setMouse(-1, -1, 0);
            g_hud->setWorldTip("");
            World::ToolResult pr = world->mouseDown(gx, gy);
            world->mouseUp(gx, gy);
            check("clicking the platform opens its tank", pr.exhibit == mt->id);
          }
        // and back down
        world->pointToWindow(mx, my, mt->wallTop() - 0.25f, px, py);
        handleToolResult(world->mouseDown(px, py), rm);
        world->mouseMove(px, py + 3 * step + step / 4);
        handleToolResult(world->mouseUp(px, py + 3 * step + step / 4), rm);
        check("and dragged back down", mt->wallSub == top0);
        world->mouseMove(0, 0);
        world->getFences().update(5.0f);
      }
    }
    // Turning the view keeps the spot in the middle where it is, all four
    // ways round
    {
      // (on flat ground beside the tank: over its pit the spot under the
      // middle is its floor or its rim, by the turn)
      world->centreOn(tx - 1.5f, ty + 1.0f);
      float ax = 0, ay = 0;
      bool ok = world->viewCentre(ax, ay);
      std::string spots;
      for (int turn = 0; turn < 4; turn++) {
        world->rotateView(1);
        float bx = 0, by = 0;
        ok = world->viewCentre(bx, by) && ok && std::fabs(ax - bx) < 0.75f && std::fabs(ay - by) < 0.75f;
        char b[48];
        std::snprintf(b, sizeof b, " %.1f,%.1f", bx, by);
        spots += b;
        capture(("rot_tank_" + std::to_string((turn + 1) % 4)).c_str());
        {
          float vx, vy;
          world->viewCentre(vx, vy);
          world->centreOn(tx + 1.0f, ty + 1.0f);
          capture(("plat_r" + std::to_string(world->getRenderer().getRotation())).c_str());
          world->centreOn(vx, vy);
        }
      }
      char a0[48];
      std::snprintf(a0, sizeof a0, "%.1f,%.1f ->", ax, ay);
      check(("rotating keeps the view's spot (" + std::string(a0) + spots + ")").c_str(), ok);
    }
    // A glass tank (Black Bar and Glass), 3 x 2, as made, then the base
    // lifted to the ground and the walls up: all four ways round, and back
    // and forth, to catch what turning does to it
    {
      auto [gx, gy] = clearSpot(2, tx, ty);
      note(("glass tank spot " + std::to_string(gx) + "," + std::to_string(gy)).c_str());
      if (gx >= 0) {
        lay("tankwall", {{gx, gy}, {gx + 2, gy}, {gx + 2, gy + 2}, {gx, gy + 2}, {gx, gy}});
        enter();
        const Fences::Exhibit *gt = nullptr;
        for (const Fences::Exhibit &x : fences.exhibits())
          if (x.tank && x.tiles.count({gx, gy}))
            gt = &x;
        check("a glass tank made", gt != nullptr);
        if (gt) {
          world->getFences().update(5.0f);
          world->centreOn(gx + 1.5f, gy + 1.0f);
          float vx = 0, vy = 0;
          world->viewCentre(vx, vy);
          const int seq[] = {1, 1, -1, 1, 1, 1, -1, -1};
          for (int i = 0; i < 8; i++) {
            world->rotateView(seq[i]);
            capture(("glass_made_" + std::to_string(i)).c_str());
          }
          int id = gt->id;
          while (fences.canAdjustBase(id, 1))
            fences.adjustBase(id, 1);
          for (int i = 0; i < 4; i++)
            fences.adjustWall(id, 1);
          world->getFences().update(20.0f);
          for (int i = 0; i < 8; i++) {
            world->rotateView(seq[i]);
            capture(("glass_up_" + std::to_string(i)).c_str());
          }
          world->rotateView(-4); // (both sequences' net turns)
          SDL_Log("[PanelShots] glass tank ground %d floor %d top %d", gt->groundHeight, gt->floorHeight,
                  gt->wallSub);
          world->centreOn(vx, vy);
          // Taken away again (the tests after count the exhibits)
          fences.remove({true, gx, gy}, world->getMap());
          fences.updateExhibits(world->getMap(), 1, 0, 1);
        }
      }
    }
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
        if (x.tank && x.tiles.count({tx, ty}))
          tex = &x;
      if (tex) {
        int top = tex->wallSub;
        double cash = g_sim.cash();
        click(4875);
        check("tank wall up a step, for its price",
              tex->wallSub == top + 1 &&
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
                tex->floorHeight, tex->wallSub, top);
        check("tank wall back down", tex->wallSub == top);
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
        // The filter and the tank all four ways round (its hose to the tank,
        // the platform inside)
        g_hud->setPanelOpen(30, false);
        {
          float vx = 0, vy = 0;
          bool had = world->viewCentre(vx, vy);
          world->centreOn(at.first + 0.5f, at.second + 0.5f);
          for (int turn = 1; turn <= 4; turn++) {
            world->rotateView(1);
            capture(("rot_filter_" + std::to_string(turn % 4)).c_str());
          }
          const int seq[] = {1, 1, -1, 1, -1, -1, -1, 1, 1, 1, -1, 1};
          for (int i = 0; i < 12; i++) {
            world->rotateView(seq[i]);
            capture(("rot_tank_seq" + std::to_string(i)).c_str());
          }
          world->rotateView(-2); // (the sequence's net turn)
          if (had)
            world->centreOn(vx, vy);
        }
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
    check(("exhibits made (" + std::to_string(fences.exhibitCount()) + ")").c_str(), fences.exhibitCount() == 3); // (with the glass tank)
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
            // (9, or 10 where a point by the test tank's pit picks the next
            // grid point over)
            fences.preview.size() >= 9 && fences.preview.size() <= 10 && turns > 1);
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
          // (under the cursor first: lit, as an animal is)
          world->mouseMove((px + qx) / 2, (py + qy) / 2 - 12);
          check("the gate under the cursor is lit", fences.hoverGate == e);
          capture("gate_hover_lit");
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
      // The bulldozer over a rock: red, its name, 80% back; clicked, gone
      {
        const auto &objs = world->getObjects().objects();
        int rock = -1;
        for (size_t i = 0; i < objs.size() && rock < 0; i++)
          if (objs[i].className == "objects" && objs[i].typeName.find("rock") != std::string::npos)
            rock = static_cast<int>(i);
        check("a rock on the map", rock >= 0);
        if (rock >= 0) {
          float ox = objs[rock].x, oy = objs[rock].y;
          std::string type = objs[rock].typeName;
          world->centreOn(ox, oy);
          const MapTile *t = map.getTile(static_cast<int>(ox), static_cast<int>(oy));
          int px, py;
          world->pointToWindow(ox, oy, t ? (float)t->cornerHeight[0] : 0.0f, px, py);
          py -= 8;
          world->setBulldozer(true);
          world->mouseMove(px, py);
          check(("bulldozer over a rock (" + type + "): " + world->bulldozeName() + " $" +
                 std::to_string(world->hoverCost())).c_str(),
                !world->bulldozeName().empty() && world->hoverCost() > 0);
          int refund = world->hoverCost();
          g_hud->setWorldTip(world->bulldozeName());
          g_hud->setMouse(px, py, SDL_GetTicks() - 1000);
          g_mouseX = px;
          g_mouseY = py;
          capture("bulldoze_rock");
          g_hud->setMouse(-1, -1, 0);
          g_hud->setWorldTip("");
          size_t before = objs.size();
          double cash0 = g_sim.cash();
          handleToolResult(world->mouseDown(px, py), rm);
          check(("bulldozed: the rock gone, $" + std::to_string(std::lround(g_sim.cash() - cash0)) + " back").c_str(),
                world->getObjects().objects().size() == before - 1 && std::lround(g_sim.cash() - cash0) == refund);
          world->setBulldozer(false);
        }
      }
      // The bulldozer lifts a tile, 80% back
      world->setBulldozer(true);
      world->setPathTool("", 10);
      at(x0 + 2, y0, px, py);
      // Over it first: red, its name, what would come back (as the original)
      world->mouseMove(px, py);
      check(("bulldozer over a path tile: " + world->bulldozeName() + " $" + std::to_string(world->hoverCost())).c_str(),
            !world->bulldozeName().empty() && world->hoverCost() == 8);
      g_hud->setWorldTip(world->bulldozeName());
      g_hud->setMouse(px, py, SDL_GetTicks() - 1000);
      g_mouseX = px;
      g_mouseY = py;
      capture("bulldoze_path");
      g_hud->setMouse(-1, -1, 0);
      g_hud->setWorldTip("");
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
      const int ground0 = map.getTile(lx, ly)->cornerHeight[0];
      int px, py;
      at(lx, ly, px, py);
      world->mouseMove(px, py);
      double cash = g_sim.cash();
      handleToolResult(world->mouseDown(px, py), rm);
      // (aimed at the deck as drawn, at its height)
      for (int i = 1; i <= 9; i++) {
        world->pointToWindow(lx + i + 0.5f, ly + 0.5f, ground0 + std::min(i, 3) - (i <= 3 ? 0.5f : 0.0f), px, py);
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
      // Click, click: from the ground over to a path, the stairs made at
      // both ends; then it carries on from there until Esc
      {
        map.setPath(lx + 8, ly - 1, "path");
        world->adjustBuildHeight(2);
        at(lx + 1, ly - 1, px, py);
        world->mouseMove(px, py);
        handleToolResult(world->mouseDown(px, py), rm);
        handleToolResult(world->mouseUp(px, py), rm);
        check("a click starts a walkway", world->isLayingLine());
        at(lx + 8, ly - 1, px, py);
        world->mouseMove(px, py);
        g_mouseX = px;
        g_mouseY = py;
        capture("walkway_click_preview");
        handleToolResult(world->mouseDown(px, py), rm);
        handleToolResult(world->mouseUp(px, py), rm);
        std::string over;
        for (int i = 2; i <= 7; i++) {
          const Walkways::Deck *d = ww.at(lx + i, ly - 1);
          over += d ? std::to_string(*std::max_element(d->h, d->h + 4) - ground) : std::string("-");
        }
        check(("click, click: up and down to the path (" + over + ")").c_str(), over == "122221");
        check("it carries on from its end", world->isLayingLine());
        world->cancelLine();
        check("Esc stops it", !world->isLayingLine());
        world->adjustBuildHeight(-2);
        // A walkway one unit up (stairs, a deck, stairs), as a player lays
        // a low one
        world->adjustBuildHeight(1);
        at(lx + 1, ly + 1, px, py);
        world->mouseMove(px, py);
        handleToolResult(world->mouseDown(px, py), rm);
        handleToolResult(world->mouseUp(px, py), rm);
        at(lx + 9, ly + 1, px, py);
        world->mouseMove(px, py);
        handleToolResult(world->mouseDown(px, py), rm);
        handleToolResult(world->mouseUp(px, py), rm);
        world->cancelLine();
        world->adjustBuildHeight(-1);
        std::string low;
        for (int i = 1; i <= 9; i++) {
          const Walkways::Deck *d = ww.at(lx + i, ly + 1);
          low += d ? std::to_string(*std::max_element(d->h, d->h + 4) - ground) : (map.isPath(lx + i, ly + 1) ? "p" : "-");
        }
        note(("low walkway " + low).c_str());
      }
      // A plain path drag that meets a raised block (cliffs all round):
      // stairs made up onto it, and off it again the other way; and one
      // dragged onto the low walkway's side
      {
        world->adjustBuildHeight(-12);
        // A strip of ground made flat, a 4 x 3 block on it two units up
        int sx0 = -1, sy0 = -1;
        for (int y = 5; y < map.getHeight() - 8 && sx0 < 0; y++)
          for (int x = 5; x < map.getWidth() - 16 && sx0 < 0; x++) {
            bool ok = true;
            for (int i = -1; i < 13 && ok; i++)
              for (int j = -1; j < 4 && ok; j++) {
                ok = world->getFences().tileFit(x + i, y + j, map) == Fences::Fit::Ok &&
                     !map.isPath(x + i, y + j) && world->getFences().exhibitAt(x + i, y + j) < 0 &&
                     !ww.at(x + i, y + j);
                for (const PlacedObjects::Object &o : world->getObjects().objects())
                  if (ok && std::fabs(o.x - (x + i + 0.5f)) < 2.5f && std::fabs(o.y - (y + j + 0.5f)) < 2.5f)
                    ok = false;
              }
            if (ok) {
              sx0 = x;
              sy0 = y;
            }
          }
        check("ground for the block test", sx0 >= 0);
        if (sx0 >= 0) {
          int g = map.getTile(sx0, sy0)->cornerHeight[0];
          for (int i = -1; i < 13; i++)
            for (int j = -1; j < 4; j++)
              if (MapTile *t = map.getTileMutable(sx0 + i, sy0 + j)) {
                bool block = i >= 8 && i <= 11 && j >= 0 && j <= 2;
                for (int c = 0; c < 4; c++)
                  t->cornerHeight[c] = g + (block ? 2 : 0);
                t->height = g + (block ? 2 : 0);
              }
          map.touch();
          world->reindex();
          world->setPathTool("path", 25);
          auto drag = [&](int x0, int y0, int x1, int y1) {
            int px, py;
            at(x0, y0, px, py);
            world->mouseMove(px, py);
            handleToolResult(world->mouseDown(px, py), rm);
            int steps = std::abs(x1 - x0) + std::abs(y1 - y0);
            for (int k = 1; k <= steps; k++) {
              int cx = x0 + (x1 - x0) * k / std::max(1, steps), cy = y0 + (y1 - y0) * k / std::max(1, steps);
              const MapTile *t = map.getTile(cx, cy);
              world->pointToWindow(cx + 0.5f, cy + 0.5f, t ? (float)t->cornerHeight[0] : 0.0f, px, py);
              world->mouseMove(px, py);
            }
            g_mouseX = px;
            g_mouseY = py;
          };
          auto row = [&](int y) {
            std::string r;
            for (int i = 0; i < 12; i++) {
              const Walkways::Deck *d = ww.at(sx0 + i, y);
              r += d ? std::to_string(*std::max_element(d->h, d->h + 4) - g)
                     : map.isPath(sx0 + i, y) ? "p" : "-";
            }
            return r;
          };
          world->centreOn(sx0 + 6.0f, sy0 + 1.0f);
          drag(sx0, sy0, sx0 + 11, sy0);
          capture("smart_up_preview");
          handleToolResult(world->mouseUp(g_mouseX, g_mouseY), rm);
          std::string up = row(sy0);
          check(("a path drawn up to a raised block gets stairs (" + up + ")").c_str(), up == "pppppp12pppp");
          drag(sx0 + 11, sy0 + 2, sx0, sy0 + 2);
          handleToolResult(world->mouseUp(g_mouseX, g_mouseY), rm);
          std::string down = row(sy0 + 2);
          check(("and drawn off it, stairs down (" + down + ")").c_str(), down == "pppppp12pppp");
          // Carrying on off the end of a raised walkway: a click on the end
          // deck's side starts from the deck (lit, not the ground under it)
          {
            const int ry = sy0 + 3;
            world->adjustBuildHeight(3);
            int px, py;
            at(sx0, ry, px, py);
            world->mouseMove(px, py);
            handleToolResult(world->mouseDown(px, py), rm);
            handleToolResult(world->mouseUp(px, py), rm);
            world->pointToWindow(sx0 + 5.5f, ry + 0.5f, g + 3.0f, px, py);
            world->mouseMove(px, py);
            handleToolResult(world->mouseDown(px, py), rm);
            handleToolResult(world->mouseUp(px, py), rm);
            world->cancelLine();
            std::string first = row(ry);
            // the end deck's front face, halfway down
            world->pointToWindow(sx0 + 5.5f, ry + 1.0f, g + 1.5f, px, py);
            world->mouseMove(px, py);
            g_mouseX = px;
            g_mouseY = py;
            capture("walkway_end_hover");
            handleToolResult(world->mouseDown(px, py), rm);
            handleToolResult(world->mouseUp(px, py), rm);
            world->pointToWindow(sx0 + 9.5f, ry + 0.5f, g + 3.0f, px, py);
            world->mouseMove(px, py);
            g_mouseX = px;
            g_mouseY = py;
            capture("walkway_continue_preview");
            handleToolResult(world->mouseDown(px, py), rm);
            handleToolResult(world->mouseUp(px, py), rm);
            world->cancelLine();
            std::string both = row(ry);
            check(("carrying on off a raised walkway's end (" + first + " -> " + both + ")").c_str(),
                  both.substr(0, 10) == "p123333333");
            world->adjustBuildHeight(-12);
          }
          world->setPathTool("", 0);
          world->mouseMove(0, 0);
          capture("smart_block");
          for (int turn = 1; turn <= 4; turn++) {
            world->rotateView(1);
            capture(("smart_block_r" + std::to_string(turn % 4)).c_str());
          }
        }
      }
      // The Paths tab: Concrete picked, its walkway arrows raise the height
      {
        g_hud->showPanel(8, "paths");
        bool picked = g_hud->selectBuyItem(8, "paths/path.ai");
        world->setPathTool("path", 25);
        UiButton *up = dynamic_cast<UiButton *>(g_hud->getElementById(3259));
        check("paths: the walkway arrows show for Concrete", picked && up && !up->isHidden());
        if (up && up->onClick) {
          up->onClick();
          up->onClick();
        }
        check(("the arrow raises the walkway (+" + std::to_string(world->getBuildHeight()) + ")").c_str(),
              world->getBuildHeight() == 2);
        g_hud->setWalkwayLabel("Walkway: +" + std::to_string(world->getBuildHeight()));
        capture("paths_walkway_arrows");
        world->adjustBuildHeight(-12);
        g_hud->selectBuyItem(8, "paths/dirtpath.ai");
        check("not for Dirt Path", up && up->isHidden());
        g_hud->setPanelOpen(8, false);
      }
      // Bends: a ground path dragged corner to corner bends once (first the
      // way the drag started); Tab bends it the other way; a walkway
      // dragged the same way bends too, level at the turn
      {
        world->adjustBuildHeight(-12);
        int bx0 = -1, by0 = -1;
        for (int y = 5; y < map.getHeight() - 12 && bx0 < 0; y++)
          for (int x = 5; x < map.getWidth() - 12 && bx0 < 0; x++) {
            bool ok = true;
            for (int i = 0; i < 8 && ok; i++)
              for (int j = 0; j < 8 && ok; j++) {
                ok = world->getFences().tileFit(x + i, y + j, map) == Fences::Fit::Ok &&
                     !map.isPath(x + i, y + j) && world->getFences().exhibitAt(x + i, y + j) < 0;
                for (int dj = -4; dj <= 4 && ok; dj++)
                  for (int di = -4; di <= 4 && ok; di++)
                    ok = !ww.at(x + i + di, y + j + dj);
                for (const PlacedObjects::Object &o : world->getObjects().objects())
                  if (ok && std::fabs(o.x - (x + i + 0.5f)) < 1.0f && std::fabs(o.y - (y + j + 0.5f)) < 1.0f)
                    ok = false;
              }
            if (ok) {
              bx0 = x;
              by0 = y;
            }
          }
        check("ground for the bend test", bx0 >= 0);
        if (bx0 >= 0) {
          int g = map.getTile(bx0, by0)->cornerHeight[0];
          for (int i = 0; i < 8; i++)
            for (int j = 0; j < 8; j++)
              if (MapTile *t = map.getTileMutable(bx0 + i, by0 + j)) {
                for (int c = 0; c < 4; c++)
                  t->cornerHeight[c] = g;
                t->height = g;
              }
          map.touch();
          world->reindex();
          world->centreOn(bx0 + 4.0f, by0 + 4.0f);
          world->setPathTool("path", 25);
          auto tileAt = [&](int tx, int ty, float h, int &px, int &py) {
            world->pointToWindow(tx + 0.5f, ty + 0.5f, h, px, py);
          };
          // ground: press, move one tile along x, then to the far corner
          int px, py;
          tileAt(bx0, by0, (float)g, px, py);
          world->mouseMove(px, py);
          handleToolResult(world->mouseDown(px, py), rm);
          tileAt(bx0 + 1, by0, (float)g, px, py);
          world->mouseMove(px, py);
          tileAt(bx0 + 5, by0 + 4, (float)g, px, py);
          world->mouseMove(px, py);
          g_mouseX = px;
          g_mouseY = py;
          capture("bend_ground_preview");
          world->cyclePathMode(1);
          capture("bend_ground_other");
          bool other = map.isPath(bx0, by0 + 4) || true;
          (void)other;
          world->cyclePathMode(-1);
          handleToolResult(world->mouseUp(px, py), rm);
          bool corner = map.isPath(bx0 + 5, by0) && map.isPath(bx0 + 3, by0) && map.isPath(bx0 + 5, by0 + 3) &&
                        !map.isPath(bx0, by0 + 4) && !map.isPath(bx0 + 3, by0 + 2);
          check("a ground path dragged corner to corner bends once", corner);
          // walkway at +2: click, then click at the far corner, as drawn
          world->adjustBuildHeight(2);
          tileAt(bx0, by0 + 1, (float)g, px, py);
          // (the first press lands on the ground path: start beside it)
          tileAt(bx0 + 1, by0 + 2, (float)g, px, py);
          world->mouseMove(px, py);
          handleToolResult(world->mouseDown(px, py), rm);
          handleToolResult(world->mouseUp(px, py), rm);
          tileAt(bx0 + 2, by0 + 2, g + 1.0f, px, py);
          world->mouseMove(px, py);
          tileAt(bx0 + 6, by0 + 6, g + 2.0f, px, py);
          world->mouseMove(px, py);
          g_mouseX = px;
          g_mouseY = py;
          capture("bend_walkway_preview");
          handleToolResult(world->mouseDown(px, py), rm);
          handleToolResult(world->mouseUp(px, py), rm);
          world->cancelLine();
          const Walkways::Deck *turnDeck = ww.at(bx0 + 6, by0 + 2);
          const Walkways::Deck *after = ww.at(bx0 + 6, by0 + 4);
          bool flatTurn = turnDeck && turnDeck->h[0] == g + 2 && turnDeck->h[1] == g + 2 &&
                          turnDeck->h[2] == g + 2 && turnDeck->h[3] == g + 2;
          check("a walkway dragged corner to corner bends, level at the turn", flatTurn && after);
          world->adjustBuildHeight(-12);
          world->setPathTool("", 0);
          world->mouseMove(0, 0);
          capture("bend_built");
        }
      }
      world->setPathTool("", 0);
      // The bulldozer over a deck: red, its name, 80% of its price
      {
        world->setBulldozer(true);
        world->pointToWindow(lx + 5.5f, ly + 0.5f, ground + 3.0f, px, py);
        world->mouseMove(px, py);
        check(("bulldozer over a walkway: " + world->bulldozeName() + " $" + std::to_string(world->hoverCost())).c_str(),
              !world->bulldozeName().empty() && world->hoverCost() == 40);
        g_hud->setWorldTip(world->bulldozeName());
        g_hud->setMouse(px, py, SDL_GetTicks() - 1000);
        g_mouseX = px;
        g_mouseY = py;
        capture("bulldoze_walkway");
        g_hud->setMouse(-1, -1, 0);
        g_hud->setWorldTip("");
        world->setBulldozer(false);
      }
      world->mouseMove(0, 0);
      capture("walkway_built");
      // Zoomed out: drawn 1:1 and shrunk smoothly, not shrunk as drawn
      {
        float saved = world->getRenderer().getCamera().zoom;
        world->getRenderer().getCamera().zoom = 0.5f;
        capture("zoomed_out_half");
        world->getRenderer().getCamera().zoom = 0.3f;
        capture("zoomed_out_third");
        world->getRenderer().getCamera().zoom = saved;
      }
      {
        float vx = 0, vy = 0;
        bool had = world->viewCentre(vx, vy);
        for (int turn = 1; turn <= 4; turn++) {
          world->rotateView(1);
          capture(("rot_walk_" + std::to_string(turn % 4)).c_str());
        }
        // Back and forth, as a player turns it (left, right, again)
        const int seq[] = {1, 1, -1, 1, -1, -1, -1, 1, 1, 1, -1, 1};
        for (int i = 0; i < 12; i++) {
          world->rotateView(seq[i]);
          capture(("rot_walk_seq" + std::to_string(i)).c_str());
        }
        world->rotateView(-2); // (the sequence's net turn)
        if (had)
          world->centreOn(vx, vy);
      }
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

  // Animals: zebras adopted with the male and female buttons (green over an
  // exhibit, red with the original's message elsewhere), named and paid
  // for; they walk about, eat the food put down, open Animal Information;
  // one sold for its price by its happiness, one moved
  {
    Animals &an = world->getAnimals();
    WorldMap &map = world->getMap();
    Fences &fn = world->getFences();
    // The game's sounds play (uiclick.wav)
    Mix_HaltChannel(-1);
    Sound::get().play("sounds/uiclick");
    check("sounds play (uiclick.wav)", Mix_Playing(-1) > 0);
    int zebra = an.typeOfFile("animals/zebra.ai");
    check(("animal types loaded (" + std::to_string(an.types().size()) + ")").c_str(),
          zebra >= 0 && an.types().size() > 40 && an.isZooAnimal(zebra));
    const Fences::Exhibit *pen = nullptr;
    for (const Fences::Exhibit &e : fn.exhibits())
      if (e.named && !e.tank && (!pen || e.tiles.size() > pen->tiles.size()))
        pen = &e;
    std::vector<std::pair<int, int>> inside;
    if (pen)
      for (auto [x, y] : pen->tiles)
        if (an.canPlace(zebra, x + 0.5f, y + 0.5f, map, fn) == Fences::Fit::Ok)
          inside.push_back({x, y});
    check("an exhibit to put animals in", inside.size() >= 3);
    auto tileWindow = [&](int x, int y, int &px, int &py) {
      const MapTile *t = map.getTile(x, y);
      float h = t ? (t->cornerHeight[0] + t->cornerHeight[1] + t->cornerHeight[2] + t->cornerHeight[3]) / 4.0f : 0;
      world->pointToWindow(x + 0.5f, y + 0.5f, h, px, py);
    };
    if (zebra >= 0 && inside.size() >= 3) {
      int penId = pen->id;
      for (const Fences::Exhibit &e : fn.exhibits())
        if (e.id == penId)
          world->centreOn(inside[0].first + 0.5f, inside[0].second + 0.5f);
      // The panel: the zebra picked, then the male button
      g_hud->showPanel(3, "animals");
      g_hud->selectBuyItem(3, "animals/zebra.ai");
      click(2000);
      bool female = true;
      std::string pickedFile = g_hud->animalToolFile(female);
      check("the male button takes the zebra", pickedFile == "animals/zebra.ai" && !female);
      world->setAnimalTool(zebra, false);
      // Off the exhibit: red, $0; on it: green, -$800
      int px, py;
      std::pair<int, int> out = {-1, -1};
      for (int r = 1; r < 12 && out.first < 0; r++)
        for (int d = -r; d <= r && out.first < 0; d++) {
          int x = inside[0].first + d, y = inside[0].second - r;
          if (fn.exhibitAt(x, y) < 0 && map.getTile(x, y))
            out = {x, y};
        }
      tileWindow(out.first, out.second, px, py);
      world->mouseMove(px, py);
      capture("animal_ghost_red");
      check("off the exhibit: red, $0",
            an.previewFit != Fences::Fit::Ok && world->hoverCost() == 0);
      World::ToolResult miss = world->mouseDown(px, py);
      check("off the exhibit: the original's message", miss.messageId == 10108 && miss.animalCost == 0);
      tileWindow(inside[0].first, inside[0].second, px, py);
      world->mouseMove(px, py);
      capture("animal_ghost_green");
      check("over the exhibit: green, -$800", an.previewFit == Fences::Fit::Ok && world->hoverCost() == 800);
      double cash = g_sim.cash();
      World::ToolResult r1 = world->mouseDown(px, py);
      handleToolResult(r1, rm);
      world->setAnimalTool(zebra, true);
      tileWindow(inside[inside.size() / 2].first, inside[inside.size() / 2].second, px, py);
      world->mouseMove(px, py);
      World::ToolResult r2 = world->mouseDown(px, py);
      handleToolResult(r2, rm);
      world->setAnimalTool(-1, false);
      g_hud->clearAnimalPick();
      g_hud->showPanel(0, "");
      std::vector<int> ids;
      for (const Animals::Member &m : an.members())
        ids.push_back(m.id);
      check(("two zebras adopted, $" + std::to_string(std::lround(cash - g_sim.cash())) + " paid").c_str(),
            ids.size() == 2 && std::lround(cash - g_sim.cash()) == 1600);
      const Animals::Member *z1 = ids.size() == 2 ? an.member(ids[0]) : nullptr;
      const Animals::Member *z2 = ids.size() == 2 ? an.member(ids[1]) : nullptr;
      check(("named " + (z1 ? z1->name : std::string("?")) + ", " + (z2 ? z2->name : std::string("?"))).c_str(),
            z1 && z2 && z1->name == "Plains Zebra 1" && z2->name == "Plains Zebra 2" && !z1->female && z2->female);
      capture("animals_adopted");
      // As in the original's test: a flat grass exhibit with nothing in it
      // and two zebras rates 48 (the original showed about 46%), and the
      // zookeeper says just what it said there
      if (z1 && z2) {
        Fences::Exhibit *pe = fn.exhibit(penId);
        bool clear = true;
        for (const PlacedObjects::Object &o : world->getObjects().objects())
          if (!o.fence && pe->tiles.count({static_cast<int>(std::floor(o.x)), static_cast<int>(std::floor(o.y))}))
            clear = false;
        world->getItems().removeIn(penId, ZooItems::Kind::Dung);
        world->getItems().removeIn(penId, ZooItems::Kind::Food);
        std::map<std::pair<int, int>, MapTile> unflattened;
        for (auto [x, y] : pe->tiles)
          if (const MapTile *t = map.getTile(x, y))
            unflattened[{x, y}] = *t;
        int flat = 1 << 30;
        for (auto [x, y] : pe->tiles)
          if (const MapTile *t = map.getTile(x, y))
            for (int c = 0; c < 4; c++)
              flat = std::min(flat, static_cast<int>(t->cornerHeight[c]));
        for (auto [x, y] : pe->tiles)
          if (MapTile *t = map.getTileMutable(x, y)) {
            t->terrainType = 0;
            for (int c = 0; c < 4; c++)
              t->cornerHeight[c] = static_cast<uint8_t>(flat);
          }
        an.markDirty();
        int suit = an.suitability(ids[0], map, fn);
        check(("bare grass exhibit, two zebras: suitability " + std::to_string(suit)).c_str(), !clear || suit == 48);
        std::vector<std::pair<std::string, bool>> advice = an.advice(ids[0], map, fn);
        std::vector<std::pair<std::string, bool>> want = {
            {"There is too much grass terrain in the exhibit for Plains Zebra 1.", true},
            {"There is not enough savannah grass terrain in the exhibit for Plains Zebra 1.", true},
            {"More animals of the same type would make Plains Zebra 1 happier.", false},
            {"Plains Zebra 1 needs more foliage in its exhibit to keep it happy.", false},
            {"Plains Zebra 1 would be happier with more rocks in the exhibit.", false},
            {"There is not enough fresh water in the exhibit for Plains Zebra 1.", false},
            {"There are not enough shelters in this exhibit.", false}};
        // (scrolled down, the original's list ends with fresh water and
        // shelters; a small exhibit also says it's not big enough)
        if (static_cast<int>(pe->tiles.size()) / 2 < an.types()[zebra].animalDensity)
          want.insert(want.begin() + 4,
                      {"The exhibit is not big enough for the number of animals it currently holds.", false});
        std::string got;
        for (auto &[text, red] : advice)
          got += std::string(red ? "[red] " : "") + text + " | ";
        SDL_Log("[Animals] advice: %s", got.c_str());
        check("and the original's recommendations", !clear || advice == want);
        // (the ground put back as it was: no cliffs left round the pen)
        for (auto &[pos, t] : unflattened)
          *map.getTileMutable(pos.first, pos.second) = t;
        map.touch();
        an.markDirty();
      }
      if (z1 && z2) {
        int id1 = ids[0], id2 = ids[1];
        // A minute of play: they get about (happy zebras walk; calm ones
        // only graze and lie down)
        if (Animals::Member *m = an.member(id1))
          m->happiness = 100;
        double clock = 0;
        float x0 = z1->x, y0 = z1->y, travelled = 0;
        std::set<std::string> anims;
        for (int i = 0; i < 600; i++) {
          clock += 0.1;
          const Animals::Member *m = an.member(id1);
          float bx = m->x, by = m->y;
          an.update(0.1f, clock, map, fn);
          m = an.member(id1);
          travelled += std::hypot(m->x - bx, m->y - by);
          anims.insert(m->anim);
          if (i == 300)
            capture("animals_walking");
        }
        std::string played;
        for (const std::string &a : anims)
          played += a + " ";
        SDL_Log("[Animals] zebra 1 played: %s, travelled %.1f tiles", played.c_str(), travelled);
        check(("zebras walk about (" + std::to_string(travelled).substr(0, 4) + " tiles)").c_str(), travelled > 2.0f);
        check(("and play their animations: " + played).c_str(), anims.size() >= 3);
        (void)x0;
        (void)y0;
        // Hungry, with food down: it goes and eats it
        if (Animals::Member *m = an.member(id1))
          m->hunger = 90;
        auto [fx, fy] = inside[inside.size() - 1];
        int food = world->getItems().addFood(penId, fx + 0.5f, fy + 0.5f, 2, "grass");
        float before = world->getItems().foodIn(penId);
        double ateBefore = an.member(id1)->lastAte;
        bool shotEating = false;
        for (int i = 0; i < 900 && an.member(id1)->lastAte == ateBefore; i++) {
          clock += 0.1;
          an.update(0.1f, clock, map, fn);
          if (!shotEating && an.member(id1)->anim == "eat") {
            shotEating = true;
            world->centreOn(an.member(id1)->x, an.member(id1)->y);
            capture("animal_eating");
          }
        }
        check(("hungry, it eats the food put down (food " + std::to_string(std::lround(before)) + " -> " +
               std::to_string(std::lround(world->getItems().foodIn(penId))) + ")")
                  .c_str(),
              an.member(id1)->lastAte > ateBefore && world->getItems().foodIn(penId) < before &&
                  an.member(id1)->hunger < 90);
        (void)food;
        // Its panel: Status, then General
        g_hud->showAnimal(id1);
        world->centreOn(an.member(id1)->x, an.member(id1)->y);
        capture("animal_panel");
        check("Animal Information open", g_hud->isPanelOpen(6) && g_hud->shownAnimalId() == id1);
        click(3164);
        capture("animal_panel_general");
        click(3162);
        // Zookeeper Recommendations
        click(3122);
        capture("animal_advice");
        check("Zookeeper Recommendations open", g_hud->isPanelOpen(28));
        // ESC closes what's on top, one at a time, as the original: the
        // recommendations, then Animal Information; then nothing's open
        bool esc1 = g_hud->closeTopmost();
        bool afterOne = !g_hud->isPanelOpen(28) && g_hud->isPanelOpen(6);
        bool esc2 = g_hud->closeTopmost();
        check("ESC closes the recommendations, then Animal Information",
              esc1 && afterOne && esc2 && !g_hud->isPanelOpen(6));
        g_hud->showPanel(0, "");
        check("with nothing open ESC has nothing to close (the game menu opens)", !g_hud->closeTopmost());
        g_hud->showAnimal(id1);
        // Hovering one names it
        tileWindow(static_cast<int>(an.member(id2)->x), static_cast<int>(an.member(id2)->y), px, py);
        // Sold: its price by its happiness
        int refund = an.refund(id2);
        int expect = 800 * Animals::shownHappiness(*an.member(id2)) / 100;
        cash = g_sim.cash();
        g_hud->showAnimal(id2);
        click(3111);
        capture("animal_sell_ask");
        {
          Input in{};
          in.type = InputType::BUTTON;
          in.event = InputEvent::KEY_DOWN;
          in.key = SDLK_RETURN;
          std::vector<Input> inputs = {in};
          g_hud->handleInputs(inputs);
        }
        check(("sold for $" + std::to_string(refund)).c_str(),
              !an.member(id2) && refund == expect && std::lround(g_sim.cash() - cash) == refund);
        // Moved: picked up, put down elsewhere in the exhibit
        world->pickUpAnimal(id1);
        auto [mx, my] = inside[1];
        tileWindow(mx, my, px, py);
        world->mouseMove(px, py);
        world->mouseDown(px, py);
        const Animals::Member *moved = an.member(id1);
        check("moved to another spot", moved && an.carried < 0 && std::fabs(moved->x - (mx + 0.5f)) < 0.6f &&
                                           std::fabs(moved->y - (my + 0.5f)) < 0.6f);
        g_hud->showPanel(0, "");
      }
    }
  }

  // Objects: an acacia bought on the Foliage tab and put down in an
  // exhibit for its price (green, "-$125"); red, "$0" where it stands now
  {
    Fences &fn = world->getFences();
    WorldMap &map = world->getMap();
    const Fences::Exhibit *pen = nullptr;
    for (const Fences::Exhibit &e : fn.exhibits())
      if (e.named && !e.tank && (!pen || e.tiles.size() > pen->tiles.size()))
        pen = &e;
    const CatalogItem *acacia = nullptr;
    for (const CatalogItem *i : ItemCatalog::get().inCategory("foliage", false))
      if (i->file.find("acacia") != std::string::npos && !acacia)
        acacia = i;
    check("an acacia to buy", acacia != nullptr && pen != nullptr);
    if (acacia && pen) {
      size_t before = world->getObjects().objects().size();
      int placedX = -1, placedY = -1;
      world->setObjectTool(acacia->file, acacia->cost, 0);
      for (auto [x, y] : pen->tiles) {
        if (world->objectFit(acacia->file, x + 0.5f, y + 0.5f, 4) != Fences::Fit::Ok)
          continue;
        placedX = x;
        placedY = y;
        break;
      }
      if (placedX >= 0) {
        const MapTile *t = map.getTile(placedX, placedY);
        float h = t ? t->cornerHeight[0] : 0;
        int px, py;
        world->centreOn(placedX + 0.5f, placedY + 0.5f);
        world->pointToWindow(placedX + 0.5f, placedY + 0.5f, h, px, py);
        world->mouseMove(px, py);
        capture("object_ghost");
        int priceShown = world->hoverCost();
        double cash = g_sim.cash();
        handleToolResult(world->mouseDown(px, py), rm);
        check(("acacia placed for $" + std::to_string(std::lround(cash - g_sim.cash())) + " (shown " +
               std::to_string(priceShown) + ")").c_str(),
              world->getObjects().objects().size() == before + 1 && std::lround(cash - g_sim.cash()) == acacia->cost &&
                  priceShown == acacia->cost);
        // The zebra in that exhibit reacts: a smile or a frown over it, and
        // its sound
        {
          const Animals::Member *z = nullptr;
          for (const Animals::Member &m : world->getAnimals().members())
            if (m.exhibit == pen->id)
              z = &m;
          bool faced = z && !z->faces.empty();
          bool sounding = Mix_Playing(-1) > 0;
          world->centreOn(placedX + 0.5f, placedY + 0.5f);
          if (z)
            for (int i = 0; i < 4; i++)
              world->getAnimals().update(0.1f, 100.0, map, fn);
          capture("object_face");
          SDL_Log("[Animals] face after the acacia: %s", z && !z->faces.empty() ? (z->faces[0].first ? "smile" : "frown") : "none");
          check("placing it: the zebra smiles or frowns, with its sound", !z || (faced && sounding));
          for (int i = 0; i < 15 && z; i++)
            world->getAnimals().update(0.1f, 100.0, map, fn);
          check("the face goes after 1.25 s", !z || z->faces.empty());
        }
        world->mouseMove(px, py);
        capture("object_placed_ghost_red");
        check("where it stands now: red, $0", world->hoverCost() == 0);
      }
      world->setObjectTool("", 0, 0);
      world->hoverOff();
      capture("object_placed");
      // Bulldozed again: gone, 80% back (as the original: a $120 palm, $96)
      if (placedX >= 0) {
        size_t n = world->getObjects().objects().size();
        const PlacedObjects::Object &o = world->getObjects().objects().back();
        const MapTile *t = map.getTile(static_cast<int>(o.x), static_cast<int>(o.y));
        int px, py;
        world->pointToWindow(o.x, o.y, t ? static_cast<float>(t->cornerHeight[0]) : 0.0f, px, py);
        world->setBulldozer(true);
        world->mouseMove(px, py - 20);
        capture("object_bulldoze_hover");
        double cash = g_sim.cash();
        handleToolResult(world->mouseDown(px, py - 20), rm);
        world->setBulldozer(false);
        check(("bulldozing the acacia in the exhibit: gone, $" + std::to_string(std::lround(g_sim.cash() - cash)) + " back").c_str(),
              world->getObjects().objects().size() == n - 1 && std::lround(g_sim.cash() - cash) == acacia->cost * 8 / 10);
      }
    }
  }

  // An exhibit's wall bulldozed with a zebra in it: asked first (as the
  // original); Yes: the gate a plain fence again, the zebra loose ("Plains
  // Zebra 1 has escaped."); the wall put back: an exhibit again, the zebra
  // in it (not "needs to be put in a suitable exhibit")
  {
    Fences &fn = world->getFences();
    WorldMap &map = world->getMap();
    Animals &an = world->getAnimals();
    const Animals::Member *z = an.members().empty() ? nullptr : &an.members().front();
    const Fences::Exhibit *pen = z ? fn.exhibit(z->exhibit) : nullptr;
    check("a zebra in an exhibit to test walls with", pen != nullptr);
    if (pen) {
      int penId = pen->id, zid = z->id;
      std::string penName = pen->name;
      // A plain wall piece of it (not the gate)
      Fences::Edge wall{false, -1, -1};
      int wallType = -1;
      for (const auto &[e, p] : fn.pieces())
        if (!p.gate && p.tank < 0 && fn.exhibitOf(e) == penId) {
          wall = e;
          wallType = p.type;
        }
      check("a wall piece of it", wall.x >= 0);
      int px, py;
      float mx = wall.alongX ? wall.x + 0.5f : static_cast<float>(wall.x);
      float my = wall.alongX ? static_cast<float>(wall.y) : wall.y + 0.5f;
      const MapTile *t = map.getTile(wall.x, wall.y);
      world->centreOn(mx, my);
      world->pointToWindow(mx, my, t ? static_cast<float>(t->cornerHeight[0]) + 0.6f : 0.0f, px, py);
      world->setBulldozer(true);
      world->mouseMove(px, py);
      Fences::Edge picked;
      bool onIt = world->pickFence(px, py, picked) && picked == wall;
      if (!onIt) {
        // (straight at it, if the point missed)
        World::ToolResult r;
      }
      handleToolResult(world->mouseDown(px, py), rm);
      capture("wall_delete_ask");
      bool asked = g_hud->hasDialog();
      check("bulldozing its wall asks first (animals would escape)", onIt && asked);
      double cash = g_sim.cash();
      {
        Input in{};
        in.type = InputType::BUTTON;
        in.event = InputEvent::KEY_DOWN;
        in.key = SDLK_RETURN;
        std::vector<Input> inputs = {in};
        g_hud->handleInputs(inputs);
      }
      world->setBulldozer(false);
      bool gone = fn.at(wall) == nullptr && fn.exhibit(penId) == nullptr;
      bool gates = false;
      for (const auto &[e, p] : fn.pieces())
        if (p.gate && p.tank < 0 && fn.exhibitOf(e) < 0)
          gates = true;
      for (int i = 0; i < 5; i++)
        world->update(SDL_GetKeyboardState(nullptr), 0.1f);
      const Animals::Member *loose = an.member(zid);
      capture("wall_deleted");
      check(("Yes: the wall gone, $" + std::to_string(std::lround(g_sim.cash() - cash)) + " back, the exhibit with it").c_str(),
            gone && g_sim.cash() > cash);
      check("its gate is a plain fence piece again", !gates);
      check("the zebra is loose", loose && loose->escaped && loose->exhibit < 0);
      // A chain-link fence holds an elephant (strength 200 > its 76, not
      // jumpable) but not a climber (climbable)
      {
        Fences::Edge chain{false, -1, -1};
        for (const auto &[e, p] : fn.pieces())
          if (fn.types()[p.type].key == "chainlnk" && !p.gate && p.tank < 0 && p.life != 0.0f)
            chain = e;
        check("chain link: an elephant can't get out, a climber can",
              chain.x >= 0 && !fn.passable(chain, false, true, 76, 0) && fn.passable(chain, true, false, 0, 0));
      }
      // (the keeper put on the zebra's side: an earlier test flattened the
      // pen, leaving cliffs that can cut the zebra off from it)
      for (const Staff::Member &k : world->getStaff().members())
        if (world->getStaff().types()[k.type].kind == Staff::Kind::Keeper && loose)
          world->getStaff().moveTo(k.id, loose->x + 0.2f, loose->y + 0.2f);
      // A keeper runs to it, darts it, crates it where it lies
      bool darted = false, shotDart = false;
      const Staff::Member *keeperOnIt = nullptr;
      for (int i = 0; i < 1800; i++) {
        world->update(SDL_GetKeyboardState(nullptr), 0.1f);
        const Animals::Member *z2 = an.member(zid);
        if (!z2 || z2->boxed)
          break;
        darted = darted || z2->tranquilised;
        for (const Staff::Member &k : world->getStaff().members())
          if (k.job == Staff::Job::Catch)
            keeperOnIt = &k;
        if (keeperOnIt && keeperOnIt->anim == "fire" && !shotDart) {
          shotDart = true;
          world->centreOn(z2->x, z2->y);
          capture("keeper_darting");
        }
      }
      loose = an.member(zid);
      world->centreOn(loose->x, loose->y);
      capture("zebra_crated");
      check("a keeper darts the loose zebra and crates it", (darted || shotDart) && loose && loose->boxed && !loose->escaped);
      // Put back (the zebra moved back inside first, in case it walked out)
      fn.place(wall, wallType, 0, map);
      std::vector<int> made = fn.updateExhibits(map, 1, 0, 1);
      for (int id : made)
        if (fn.exhibit(id))
          fn.finishExhibit(id, penName, map);
      // The crate put down in the exhibit again (Move Animal): let out
      if (const Fences::Exhibit *np = made.empty() ? nullptr : fn.exhibit(made[0])) {
        auto [ix, iy] = *np->tiles.begin();
        an.carried = zid;
        an.drop(zid, ix + 0.5f, iy + 0.5f, fn);
      }
      for (int i = 0; i < 30; i++)
        world->update(SDL_GetKeyboardState(nullptr), 0.1f);
      loose = an.member(zid);
      bool inside = loose && loose->exhibit >= 0 && !loose->escaped && !loose->boxed;
      std::vector<std::pair<std::string, bool>> advice = loose ? an.advice(zid, map, fn) : decltype(advice){};
      bool needsExhibit = !advice.empty() && advice[0].first.find("needs to be put") != std::string::npos;
      capture("wall_back");
      check(("the wall back: an exhibit again (" + std::to_string(made.size()) + " made), the zebra in it").c_str(),
            made.size() == 1 && inside && !needsExhibit);
    }
  }

  // Making an exhibit bigger, as players do: a new section fenced on to
  // its side (a new exhibit, named), then the wall between taken out:
  // asked "merge ... with ...?"; the zebras' exhibit keeps its name and
  // gate, now bigger, and nobody has escaped
  {
    Fences &fn = world->getFences();
    WorldMap &map = world->getMap();
    Animals &an = world->getAnimals();
    const Animals::Member *z = nullptr;
    for (const Animals::Member &m : an.members())
      if (m.exhibit >= 0)
        z = &m;
    const Fences::Exhibit *pen = z ? fn.exhibit(z->exhibit) : nullptr;
    bool built = false;
    if (pen) {
      int penId = pen->id;
      std::string penName = pen->name;
      int x0 = 1 << 30, x1 = -1, y0 = 1 << 30, y1 = -1;
      for (auto [x, y] : pen->tiles) {
        x0 = std::min(x0, x); x1 = std::max(x1, x); y0 = std::min(y0, y); y1 = std::max(y1, y);
      }
      int type = -1;
      for (const auto &[e, p] : fn.pieces())
        if (fn.exhibitOf(e) == penId && !p.gate)
          type = p.type;
      // A section east (or west) of it, smaller than it, free ground (the
      // bigger of two joined exhibits keeps its name, as zoo.exe)
      for (int side : {1, -1}) {
        if (built)
          break;
        const int w = std::max(1, (x1 - x0 + 1) / 2);
        int sx0 = side > 0 ? x1 + 1 : x0 - w, sx1 = side > 0 ? x1 + w : x0 - 1;
        bool free = true;
        for (int y = y0; y <= y1 && free; y++)
          for (int x = sx0; x <= sx1 && free; x++)
            free = fn.tileFit(x, y, map) == Fences::Fit::Ok && fn.exhibitAt(x, y) < 0;
        for (int y = y0; y <= y1 && free; y++)
          free = fn.at(Fences::Edge{false, side > 0 ? x1 + 1 : x0, y}) != nullptr;
        if (!free)
          continue;
        for (int x = sx0; x <= sx1; x++) {
          fn.place(Fences::Edge{true, x, y0}, type, 900, map);
          fn.place(Fences::Edge{true, x, y1 + 1}, type, 900, map);
        }
        for (int y = y0; y <= y1; y++)
          fn.place(Fences::Edge{false, side > 0 ? sx1 + 1 : sx0, y}, type, 900, map);
        std::vector<int> made = fn.updateExhibits(map, 1, 0, 1);
        for (int id : made)
          fn.finishExhibit(id, "New Section", map);
        check(("a section fenced on: a new exhibit (" + std::to_string(made.size()) + ")").c_str(), made.size() == 1);
        // The wall between, bulldozed
        Fences::Edge mid{false, side > 0 ? x1 + 1 : x0, (y0 + y1) / 2};
        float mx = static_cast<float>(mid.x), my = mid.y + 0.5f;
        const MapTile *t = map.getTile(mid.x, mid.y);
        world->centreOn(mx, my);
        int px, py;
        world->pointToWindow(mx, my, t ? static_cast<float>(t->cornerHeight[0]) + 0.6f : 0.0f, px, py);
        world->setBulldozer(true);
        world->mouseMove(px, py);
        Fences::Edge picked;
        bool onIt = world->pickFence(px, py, picked) && picked == mid;
        handleToolResult(world->mouseDown(px, py), rm);
        capture("merge_ask");
        bool asked = g_hud->hasDialog();
        {
          Input in{};
          in.type = InputType::BUTTON;
          in.event = InputEvent::KEY_DOWN;
          in.key = SDLK_RETURN;
          std::vector<Input> inputs = {in};
          g_hud->handleInputs(inputs);
        }
        world->setBulldozer(false);
        for (int i = 0; i < 5; i++)
          world->update(SDL_GetKeyboardState(nullptr), 0.1f);
        const Fences::Exhibit *big = fn.exhibit(penId);
        bool gate = false;
        for (const auto &[e, p] : fn.pieces())
          gate = gate || (p.gate && fn.exhibitOf(e) == penId);
        int loose = an.escapedCount();
        capture("merged");
        // The fence panel's gate button: shown and lit with an exhibit to
        // gate; a click turns the gate tool on, another off
        {
          g_hud->setGateMode(false); // (the gate tests before left it on)
          g_hud->showPanel(8, "fence");
          std::vector<Input> none;
          g_hud->handleInputs(none);
          capture("fence_gate_button");
          UiElement *gb = g_hud->getElementById(3240);
          bool lit = gb && !gb->isHidden() && !gb->isDisabled();
          click(3240);
          bool on = g_hud->gateMode();
          click(3240);
          check("the gate button: lit with an exhibit, toggles the gate tool", lit && on && !g_hud->gateMode());
          g_hud->showPanel(0, "");
        }
        check("its wall to the section asks to merge them", onIt && asked);
        check(("merged: still " + penName + ", bigger (" + std::to_string(big ? big->tiles.size() : 0) + " tiles), its gate kept, nobody loose").c_str(),
              big && big->name == penName && big->tiles.size() > static_cast<size_t>((x1 - x0 + 1) * (y1 - y0 + 1)) && gate && loose == 0);
        built = true;
      }
    }
    check("room beside the zebras' exhibit to build a section", built);
    // The gate button: another wall of the exhibit made its gate (free:
    // the fence isn't worn), the old gate a plain fence; only one gate
    if (const Animals::Member *zz = an.members().empty() ? nullptr : &an.members().front())
      if (zz->exhibit >= 0) {
        int exId = zz->exhibit;
        Fences::Edge oldGate{false, -1, -1}, wall{false, -1, -1};
        for (const auto &[e, p] : fn.pieces()) {
          if (fn.exhibitOf(e) != exId)
            continue;
          if (p.gate)
            oldGate = e;
          else if (fn.canGate(e))
            wall = e;
        }
        int cost = wall.x >= 0 ? fn.gateCostOf(wall) : -1;
        if (wall.x >= 0)
          fn.moveGate(wall);
        int gates = 0;
        for (const auto &[e, p] : fn.pieces())
          gates += p.gate && fn.exhibitOf(e) == exId ? 1 : 0;
        check(("the gate moved to another wall ($" + std::to_string(cost) + "), one gate").c_str(),
              wall.x >= 0 && cost == 0 && fn.at(wall)->gate && (oldGate.x < 0 || !fn.at(oldGate)->gate) && gates == 1);
      }
  }

  // An unassigned zookeeper looks after the zebras by itself (as the
  // original: "Going to Exhibit 2", "Placing food"): a pile of herbivore
  // chow, $229 on Zoo Upkeep
  {
    Fences &fn = world->getFences();
    Animals &an = world->getAnimals();
    Staff &st = world->getStaff();
    int pen = -1;
    for (const Animals::Member &m : an.members())
      if (m.exhibit >= 0)
        pen = m.exhibit;
    int keeper = -1;
    for (Staff::Member &k : const_cast<std::vector<Staff::Member> &>(st.members()))
      if (st.types()[k.type].kind == Staff::Kind::Keeper) {
        k.exhibits.clear();
        k.job = Staff::Job::None;
        k.workCheck = 0;
        keeper = k.id;
      }
    world->getItems().removeIn(pen, ZooItems::Kind::Food);
    st.takeUpkeep();
    double exUpkeep0 = fn.exhibit(pen) ? fn.exhibit(pen)->upkeepTotal : 0;
    std::string food;
    double paid = 0;
    bool going = false;
    float gateOpened = 0;
    for (int i = 0; i < 3000 && pen >= 0 && keeper >= 0; i++) {
      world->update(SDL_GetKeyboardState(nullptr), 0.1f);
      paid += st.takeUpkeep();
      for (const auto &kv : fn.pieces())
        gateOpened = std::max(gateOpened, kv.second.gate ? kv.second.open : 0.0f);
      if (const Staff::Member *k = st.member(keeper))
        going = going || (k->dutyId == 10303 && k->visit == pen);
      for (const ZooItems::Item &it : world->getItems().items())
        if (it.kind == ZooItems::Kind::Food && it.exhibit == pen)
          food = it.food;
      if (!food.empty())
        break;
    }
    check(("an unassigned keeper feeds the zebras (" + food + ", $" + std::to_string(static_cast<int>(paid)) + ")").c_str(),
          going && food == "herbchow" && static_cast<int>(paid) == 229);
    check(("the gate swings open for the keeper (frame " + std::to_string(static_cast<int>(gateOpened)) + ")").c_str(),
          gateOpened >= 11.0f);
    // (on the exhibit's books too: Exhibit Information's Upkeep, as the
    // original's Exhibit 2 showed $229.00 after its keeper fed it)
    {
      const Fences::Exhibit *ex = fn.exhibit(pen);
      double add = ex ? ex->upkeepTotal - exUpkeep0 : 0;
      check(("the exhibit's upkeep: $" + std::to_string(static_cast<int>(add))).c_str(),
            ex && static_cast<int>(std::lround(add)) == 229 && ex->upkeepNow >= 229);
    }
    // A sick zebra (health 40%) and dung: the keeper heals it ("Healing
    // animal", 20% of its price) and rakes the dung up
    {
      Animals::Member *z = nullptr;
      for (const Animals::Member &m : an.members())
        if (m.exhibit == pen)
          z = an.member(m.id);
      if (z) {
        z->health = an.types()[z->type].maxHits * 0.4f;
        world->getItems().add(ZooItems::Kind::Dung, pen, z->x + 0.4f, z->y);
        int zid = z->id;
        bool healing = false;
        double cost = 0;
        st.takeUpkeep();
        for (int i = 0; i < 3000; i++) {
          world->update(SDL_GetKeyboardState(nullptr), 0.1f);
          cost += st.takeUpkeep();
          if (const Staff::Member *k = st.member(keeper))
            healing = healing || k->dutyId == 10301;
          const Animals::Member *zz = an.member(zid);
          if (zz && !an.needsHealing(*zz) && world->getItems().ofKind(ZooItems::Kind::Dung, pen).empty())
            break;
        }
        const Animals::Member *zz = an.member(zid);
        check(("a keeper heals the sick zebra and rakes the dung ($" + std::to_string(static_cast<int>(cost)) + ")").c_str(),
              healing && zz && an.shownHealth(*zz) == 100 && world->getItems().ofKind(ZooItems::Kind::Dung, pen).empty());
        const Fences::Exhibit *ex = fn.exhibit(pen);
        check("the vet bill on the exhibit's upkeep too",
              ex && std::lround(ex->upkeepTotal - exUpkeep0) == std::lround(229 + cost));
      }
    }
    (void)fn;
  }

  // Guests: they come in at the entrance (a 1 in 4 chance every 2 s at a
  // middling rating and $22), pay $22 x 3.5 ($38.50 children, as the
  // original's books), walk the paths and stop to look at the zebras
  {
    Guests &gs = world->getGuests();
    world->update(SDL_GetKeyboardState(nullptr), 0.01f);
    check("guests know where the entrance is", gs.hasEntrance());
    gs.clear();
    double cash = g_sim.cash();
    world->setEconomy(50, 22.0);
    int came = 0, gawked = 0;
    double paid = 0;
    bool offPath = false;
    std::set<int> seenIds;
    for (int i = 0; i < 900; i++) {
      world->update(SDL_GetKeyboardState(nullptr), 0.1f);
      int n = 0;
      double m = gs.takeIncome(n);
      came += n;
      paid += m;
      for (const Guests::Guest &g : gs.guests()) {
        seenIds.insert(g.id);
        if (g.state == Guests::State::Gawking)
          gawked++;
        int tx = static_cast<int>(std::floor(g.x)), ty = static_cast<int>(std::floor(g.y));
        if (g.x > -100 && !world->getMap().isPath(tx, ty))
          offPath = true;
      }
      if (i == 600 && !gs.guests().empty()) {
        const Guests::Guest &g = gs.guests().front();
        world->centreOn(g.x, g.y);
        capture("guests_walking");
      }
    }
    // Each fee is $77 or $38.50
    bool fees = came > 0;
    double adults = (paid - came * 38.5) / 38.5;
    fees = fees && std::fabs(adults - std::round(adults)) < 0.01 && adults >= 0 && adults <= came;
    check(("guests came in: " + std::to_string(came) + " in 90 s, paying $" + std::to_string(static_cast<int>(paid))).c_str(),
          came >= 3 && fees);
    check("they keep to the paths", !offPath);
    check(("and stop to look at the zebras (" + std::to_string(gawked) + " guest-frames gawking)").c_str(), gawked > 0);
    (void)cash;
    SDL_Log("[Guests] %zu now, average happiness %d", gs.guests().size(), gs.averageHappiness());
    // A hot dog stand by the path near the entrance: hungry guests buy
    // there (its $2.00 x cPriceFactor 3 = $6 a sale, Concessions)
    {
      WorldMap &map = world->getMap();
      std::string stand = "scenery/building/hdogstnd.ai";
      int sx = -1, sy = -1;
      for (int r = 1; r < 25 && sx < 0; r++)
        for (int dy = -r; dy <= r && sx < 0; dy++)
          for (int dx = -r; dx <= r && sx < 0; dx++) {
            float x = gs.guests().empty() ? 0 : 0;
            (void)x;
            int tx = 11 + dx, ty = 42 + dy;
            // beside a path tile, its 1 x 1 tile footprint on open ground
            bool byPath = map.isPath(tx + 1, ty) || map.isPath(tx - 1, ty) || map.isPath(tx, ty + 1) || map.isPath(tx, ty - 1);
            if (byPath && world->objectFit(stand, tx + 0.5f, ty + 0.5f, 4) == Fences::Fit::Ok) {
              // (a 2 x 2 stand: needs the tile right and below too)
              if (world->objectFit(stand, tx + 1.0f, ty + 1.0f, 4) == Fences::Fit::Ok) {
                sx = tx;
                sy = ty;
              }
            }
          }
      check("a spot for a hot dog stand by the path", sx >= 0);
      if (sx >= 0) {
        world->setObjectTool(stand, 175, 0);
        world->getObjects();
        const_cast<PlacedObjects &>(world->getObjects()).add(stand, sx + 1.0f, sy + 1.0f, 4);
        world->setObjectTool("", 0, 0);
        world->reindex();
        for (const Guests::Guest &g : gs.guests())
          const_cast<Guests::Guest &>(g).hunger = 80;
        double sold = 0;
        bool shot = false;
        float hungerAfter = 100;
        for (int i = 0; i < 1500; i++) {
          world->update(SDL_GetKeyboardState(nullptr), 0.1f);
          sold += gs.takeConcessions();
          int n = 0;
          gs.takeIncome(n);
          for (const Guests::Guest &g : gs.guests())
            if (g.building >= 0 && !shot && g.state == Guests::State::Using) {
              shot = true;
              world->centreOn(g.x, g.y);
              capture("guest_at_stand");
            }
          if (sold > 0)
            break;
        }
        for (const Guests::Guest &g : gs.guests())
          hungerAfter = std::min(hungerAfter, g.hunger);
        check(("a hungry guest buys a hot dog: $" + std::to_string(static_cast<int>(sold)) + " concessions").c_str(),
              sold >= 6.0 && std::fmod(sold, 6.0) < 0.01);
        // Its Building Information: "Hot Dog Stand 1", $2.00, its takings
        {
          const PlacedObjects::Object &o = world->getObjects().objects().back();
          g_hud->showBuilding(o.id);
          world->centreOn(o.x, o.y);
          capture("building_panel");

          check(("Building Information: " + o.label + ", income $" + std::to_string(static_cast<int>(o.income))).c_str(),
                g_hud->isPanelOpen(21) && o.label.rfind("Hot Dog Stand", 0) == 0 && o.income >= 6.0);
          click(4557);
          check("its price up a step ($2.25)", std::fabs(world->getObjects().objects().back().price - 2.25f) < 0.001f);
          click(4556);
          // Its paint tab: "Roof" in gold; the 24 colours (red, green, blue
          // order: sky first); sky picked, the stand is painted sky
          {
            std::vector<Input> none;
            click(4577);
            g_hud->handleInputs(none);
            capture("building_paint");
            bool paintOpen = g_hud->isPanelOpen(302);
            click(2461);
            g_hud->handleInputs(none);
            capture("building_colours");
            bool pickerOpen = g_hud->isPanelOpen(37);
            click(2401);
            g_hud->handleInputs(none);
            const PlacedObjects::Object &st = world->getObjects().objects().back();
            const PlacedObjects::Colouring *c = world->objectsMutable().colouringOf(PlacedObjects::fileOf(st));
            std::string picked = c && !st.colours.empty() && st.colours[0] >= 0 ? c->parts[0].pals[st.colours[0]] : "";
            world->centreOn(st.x, st.y);
            world->objectsMutable().hideFoliage = true; // (a tree stands in front of it)
            capture("building_painted");
            world->objectsMutable().hideFoliage = false;
            check(("paint tab, colours, " + picked).c_str(),
                  paintOpen && pickerOpen && picked.find("sky16") != std::string::npos && g_hud->isPanelOpen(302));
            click(4575);
            g_hud->handleInputs(none);
          }
          g_hud->showPanel(0, "");
        }
        // Guest Information: its panel and thoughts
        if (!gs.guests().empty()) {
          const Guests::Guest &g = gs.guests().front();
          g_hud->showGuest(g.id);
          world->centreOn(g.x, g.y);
          capture("guest_panel");
          check(("Guest Information open on " + g.name + " (" + std::to_string(g.thoughts.size()) + " thoughts)").c_str(),
                g_hud->isPanelOpen(9) && g.name.rfind("Guest ", 0) == 0);
          click(3465);
          capture("guest_thoughts");
          g_hud->showPanel(0, "");
        }
        // The Exhibit/Show List on the zebras' exhibit: each tab
        {
          int pen = -1;
          for (const Animals::Member &m : world->getAnimals().members())
            if (m.exhibit >= 0)
              pen = m.exhibit;
          if (pen >= 0) {
            g_hud->showExhibit(pen);
            capture("exhibit_status");
            click(4313);
            capture("exhibit_donations");
            click(4311);
            capture("exhibit_animals");
            click(4312);
            capture("exhibit_thoughts");
            check(("Exhibit/Show List open on exhibit " + std::to_string(pen)).c_str(), g_hud->isPanelOpen(30));
            if (const Fences::Exhibit *ex = world->getFences().exhibit(pen)) {
              char line[160];
              snprintf(line, sizeof line, "exhibit books: viewed %.0f s, donations $%.2f, popularity %d, fence %d",
                       ex->viewed, ex->donationsTotal, ex->popularity(gs.clock), world->getFences().condition(pen));
              check(line, world->getFences().condition(pen) == 0 &&
                              (ex->viewed <= 0 || ex->donationsTotal > 0 || world->getAnimals().exhibitSuitability(pen) <= 0));
            }
            g_hud->showPanel(0, "");
          }
        }
        // A restroom by the path: guests who need it go in; its Building
        // Information counts them (Visitors: Last Month / This Month /
        // Total)
        {
          WorldMap &map = world->getMap();
          std::string loo = "scenery/building/bathroom.ai";
          float lx = -1, ly = -1;
          for (int r = 1; r < 25 && lx < 0; r++)
            for (int dy = -r; dy <= r && lx < 0; dy++)
              for (int dx = -r; dx <= r && lx < 0; dx++) {
                int tx = 11 + dx, ty = 42 + dy;
                bool byPath = map.isPath(tx + 1, ty) || map.isPath(tx - 1, ty) || map.isPath(tx, ty + 1) || map.isPath(tx, ty - 1);
                for (float off : {0.5f, 1.0f})
                  if (lx < 0 && byPath && world->objectFit(loo, tx + off, ty + off, 4) == Fences::Fit::Ok) {
                    lx = tx + off;
                    ly = ty + off;
                  }
              }
          check("a spot for a restroom by the path", lx >= 0);
          if (lx >= 0) {
            world->objectsMutable().add(loo, lx, ly, 4);
            world->reindex();
            int looId = world->getObjects().objects().back().id;
            for (const Guests::Guest &g : gs.guests())
              const_cast<Guests::Guest &>(g).bathroom = 95;
            for (int i = 0; i < 2000; i++) {
              world->update(SDL_GetKeyboardState(nullptr), 0.1f);
              gs.takeConcessions();
              int n = 0;
              gs.takeIncome(n);
              const PlacedObjects::Object *o = world->objectsMutable().byId(looId);
              if (o && o->visitorsNow > 0)
                break;
            }
            const PlacedObjects::Object *o = world->objectsMutable().byId(looId);
            g_hud->showBuilding(looId);
            if (o)
              world->centreOn(o->x, o->y);
            capture("restroom_panel");
            check(("a restroom's visitors: " + std::to_string(o ? o->visitorsNow : 0) + " this month").c_str(),
                  o && o->visitorsNow > 0 && o->visitorsTotal == o->visitorsNow && g_hud->isPanelOpen(20));
            g_hud->showPanel(0, "");
          }
        }
        // The other shops: a drink stand (thirsty guests buy a soda), a gift
        // shop (happy guests buy a souvenir) and the carousel (an attraction
        // guests ride) - each takes money
        {
          WorldMap &map = world->getMap();
          std::vector<int> shops;
          std::vector<std::pair<int, int>> laid; // (test paths, taken up after)
          std::set<std::pair<int, int>> reach;
          for (const Guests::Guest &g : gs.guests())
            if (g.x > -999 && reach.empty())
              reach = gs.reachableFrom(static_cast<int>(std::floor(g.x)), static_cast<int>(std::floor(g.y)));
          auto placeByPath = [&](const std::string &file) -> int {
            int fpx = 2, fpy = 2;
            if (IniReader *ai = rm->getIniReader(file)) {
              fpx = ai->getInt("characteristics/integers", "cfootprintx", 2);
              fpy = ai->getInt("characteristics/integers", "cfootprinty", 2);
              delete ai;
            }
            for (int r = 1; r < 40; r++)
              for (int dy = -r; dy <= r; dy++)
                for (int dx = -r; dx <= r; dx++) {
                  if (std::max(std::abs(dx), std::abs(dy)) != r)
                    continue;
                  for (float off : {0.0f, 0.5f}) {
                    float x = 11 + dx + off, y = 42 + dy + off;
                    if (world->objectFit(file, x, y, 4) != Fences::Fit::Ok)
                      continue;
                    // (a path touching its footprint: its door)
                    float hx = fpx / 4.0f, hy = fpy / 4.0f;
                    int x0 = static_cast<int>(std::floor(x - hx)) - 1, x1 = static_cast<int>(std::floor(x + hx - 0.01f)) + 1;
                    int y0 = static_cast<int>(std::floor(y - hy)) - 1, y1 = static_cast<int>(std::floor(y + hy - 0.01f)) + 1;
                    bool byPath = false;
                    for (int py = y0; py <= y1 && !byPath; py++)
                      for (int px = x0; px <= x1 && !byPath; px++)
                        byPath = (py == y0 || py == y1 || px == x0 || px == x1) && reach.count({px, py});
                    if (!byPath)
                      continue;
                    if (!world->objectsMutable().add(file, x, y, 4))
                      return -1;
                    world->reindex();
                    shops.push_back(world->getObjects().objects().back().id);
                    return world->getObjects().objects().back().id;
                  }
                }
            return -1;
          };
          // (a big one: anywhere it fits, a path laid round it and some guests
          // set down on that path)
          auto placeWithPath = [&](const std::string &file) -> int {
            int fpx = 2, fpy = 2;
            if (IniReader *ai = rm->getIniReader(file)) {
              fpx = ai->getInt("characteristics/integers", "cfootprintx", 2);
              fpy = ai->getInt("characteristics/integers", "cfootprinty", 2);
              delete ai;
            }
            std::string pathType = map.getPathTypes().empty() ? std::string("stnepath") : map.getPathTypes()[0];
            for (int r = 1; r < 60; r++)
              for (int dy = -r; dy <= r; dy++)
                for (int dx = -r; dx <= r; dx++) {
                  if (std::max(std::abs(dx), std::abs(dy)) != r)
                    continue;
                  float x = 11 + dx + ((fpx / 2) % 2 ? 0.5f : 0.0f), y = 42 + dy + ((fpy / 2) % 2 ? 0.5f : 0.0f);
                  if (world->objectFit(file, x, y, 4) != Fences::Fit::Ok)
                    continue;
                  float hx = fpx / 4.0f, hy = fpy / 4.0f;
                  int x0 = static_cast<int>(std::floor(x - hx)) - 1, x1 = static_cast<int>(std::floor(x + hx - 0.01f)) + 1;
                  int y0 = static_cast<int>(std::floor(y - hy)) - 1, y1 = static_cast<int>(std::floor(y + hy - 0.01f)) + 1;
                  // (a flat ring of open ground round it, inside the zoo)
                  bool ok = true;
                  std::vector<std::pair<int, int>> ring;
                  for (int py = y0; py <= y1 && ok; py++)
                    for (int px = x0; px <= x1 && ok; px++)
                      if (py == y0 || py == y1 || px == x0 || px == x1) {
                        const MapTile *t = map.getTile(px, py);
                        ok = t && world->getFences().insideZoo(px, py) && world->getFences().exhibitAt(px, py) < 0 &&
                             t->cornerHeight[0] == t->cornerHeight[1] && t->cornerHeight[1] == t->cornerHeight[2] &&
                             t->cornerHeight[2] == t->cornerHeight[3];
                        ring.push_back({px, py});
                      }
                  if (!ok || !world->objectsMutable().add(file, x, y, 4))
                    continue;
                  for (auto [px, py] : ring)
                    if (!map.isPath(px, py)) {
                      map.setPath(px, py, pathType);
                      laid.push_back({px, py});
                    }
                  world->reindex();
                  int k = 0;
                  for (const Guests::Guest &g : gs.guests()) {
                    if (g.x < -999 || k >= 4)
                      continue;
                    Guests::Guest &gg = const_cast<Guests::Guest &>(g);
                    gg.x = ring[(k * 3) % ring.size()].first + 0.5f;
                    gg.y = ring[(k * 3) % ring.size()].second + 0.5f;
                    gg.path.clear();
                    gg.pathAt = 0;
                    gg.building = -1;
                    gg.state = Guests::State::Walking;
                    gg.playFor = 0;
                    k++;
                  }
                  shops.push_back(world->getObjects().objects().back().id);
                  return world->getObjects().objects().back().id;
                }
            return -1;
          };
          auto runUntilPaid = [&](int id, const std::function<void()> &nudge) {
            for (int i = 0; i < 4000; i++) {
              nudge();
              world->update(SDL_GetKeyboardState(nullptr), 0.1f);
              gs.takeConcessions();
              int n = 0;
              gs.takeIncome(n);
              const PlacedObjects::Object *o = world->objectsMutable().byId(id);
              if (o && o->income > 0)
                return o->income;
            }
            return 0.0;
          };
          int drink = placeByPath("scenery/building/drkstnd.ai");
          double soda = drink < 0 ? 0 : runUntilPaid(drink, [&] {
            for (const Guests::Guest &g : gs.guests())
              const_cast<Guests::Guest &>(g).thirst = std::max(g.thirst, 80.0f);
          });
          check(("a thirsty guest buys at the drink stand: $" + std::to_string(static_cast<int>(soda))).c_str(),
                drink >= 0 && soda >= 4.0);
          int gift = placeWithPath("scenery/building/giftshp.ai");
          double souvenir = gift < 0 ? 0 : runUntilPaid(gift, [&] {
            for (const Guests::Guest &g : gs.guests()) {
              Guests::Guest &gg = const_cast<Guests::Guest &>(g);
              gg.happiness = 90;
              gg.souvenirCheck = std::min(gg.souvenirCheck, 0);
            }
          });
          check(("a happy guest buys a souvenir at the gift shop: $" + std::to_string(static_cast<int>(souvenir))).c_str(),
                gift >= 0 && souvenir >= 18.0);
          int ride = placeWithPath("scenery/building/carousal.ai");
          if (ride >= 0)
            world->centreOn(world->objectsMutable().byId(ride)->x, world->objectsMutable().byId(ride)->y);
          double fun = ride < 0 ? 0 : runUntilPaid(ride, [] {});
          if (ride >= 0)
            capture("carousel_ridden");
          check(("guests ride the carousel: $" + std::to_string(static_cast<int>(fun))).c_str(), ride >= 0 && fun >= 10.0);
          // A reptile house: its Programs tab (Reptiles of the Rainforest, free,
          // $0.00, +5 / +5); Deadly Snakes of the World researched, both,
          // cheapest first, and picked: $120.00, +10 / +12
          {
            int house = placeWithPath("scenery/building/repthous.ai");
            auto textOf = [&](int id) {
              UiText *t = dynamic_cast<UiText *>(g_hud->getElementById(id));
              return t ? t->getText() : std::string();
            };
            std::vector<Input> none;
            bool firstOk = false, secondOk = false;
            if (house >= 0) {
              g_hud->showBuilding(house);
              click(4678);
              g_hud->handleInputs(none);
              capture("house_programs");
              UiElement *b2 = g_hud->getElementById(4632);
              firstOk = textOf(4640) == rm->getString(23347) && textOf(4642) == "$0.00" && textOf(4644) == "+5" &&
                        textOf(4646) == "+5" && b2 && b2->isHidden();
              ResearchProgram *snakes = nullptr;
              for (ResearchBranch &bb : Research::get().branches())
                for (ResearchCategory &c : bb.categories)
                  for (ResearchProgram &pp : c.programs)
                    if (pp.file.find("progex40") != std::string::npos)
                      snakes = &pp;
              if (snakes) {
                snakes->done = true;
                g_hud->handleInputs(none);
                click(4632);
                g_hud->handleInputs(none);
                capture("house_programs2");
                secondOk = world->objectsMutable().byId(house)->program == 1 && textOf(4640) == rm->getString(23340) &&
                           textOf(4642) == "$120.00" && textOf(4644) == "+10" && textOf(4646) == "+12";
                snakes->done = false;
              }
              g_hud->showPanel(0, "");
            }
            check(("reptile house programs: " + textOf(4640) + " " + textOf(4642)).c_str(), house >= 0 && firstOk && secondOk);
          }
          for (int id : shops) {
            int i = world->getObjects().indexOf(id);
            if (i >= 0)
              world->objectsMutable().remove(i);
          }
          for (auto [px, py] : laid)
            map.setPath(px, py, "");
          world->reindex();
          for (const Guests::Guest &g : gs.guests()) {
            Guests::Guest &gg = const_cast<Guests::Guest &>(g);
            gg.building = -1;
            // (anyone left on the taken-up paths: back to the entrance)
            if (gg.x > -999 && !map.isPath(static_cast<int>(gg.x), static_cast<int>(gg.y)) && !reach.empty()) {
              gg.x = reach.begin()->first + 0.5f;
              gg.y = reach.begin()->second + 0.5f;
              gg.path.clear();
              gg.pathAt = 0;
            }
          }
        }
        // A tour guide: a trip to the zebras with a group of guests from the
        // path, its talk there ("Speaking"), the group cheered (+30)
        {
          Staff &st = world->getStaff();
          int guideType = -1;
          for (size_t k = 0; k < st.types().size(); k++)
            if (st.types()[k].kind == Staff::Kind::Guide)
              guideType = static_cast<int>(k);
          float hx = -1, hy = -1;
          for (const Guests::Guest &g : gs.guests())
            if (g.x > -999 && world->getMap().isPath(static_cast<int>(g.x), static_cast<int>(g.y))) {
              hx = g.x;
              hy = g.y;
            }
          int guide = guideType >= 0 && hx >= 0 ? st.hire(guideType, hx, hy, world->getMap(), world->getFences()) : -1;
          check("a tour guide hired on the path among guests", guide >= 0);
          bool spoke = false, cheered = false;
          size_t most = 0;
          for (int i = 0; i < 3000 && guide >= 0; i++) {
            world->update(SDL_GetKeyboardState(nullptr), 0.1f);
            gs.takeConcessions();
            int n = 0;
            gs.takeIncome(n);
            if (const Staff::Member *m = st.member(guide)) {
              most = std::max(most, m->followers.size());
              if (m->speaking && !spoke) {
                spoke = true;
                world->centreOn(m->x, m->y);
                capture("tour_guide_speaking");
              }
            }
            for (const Guests::Guest &g : gs.guests())
              cheered = cheered || g.tourHeard;
            if (spoke && cheered)
              break;
          }
          check(("tour guide: a group of " + std::to_string(most) + ", its talk, the group cheered").c_str(),
                most > 0 && spoke && cheered);
          if (guide >= 0)
            st.fire(guide);
        }
        // A trash can by the path: over half full (13) a maintenance worker
        // empties it ("Emptying trash can")
        {
          WorldMap &map = world->getMap();
          std::string can = "scenery/other/trshcan.ai";
          float cx = -1, cy = -1;
          for (int r = 1; r < 25 && cx < 0; r++)
            for (int dy = -r; dy <= r && cx < 0; dy++)
              for (int dx = -r; dx <= r && cx < 0; dx++) {
                int tx = 11 + dx, ty = 42 + dy;
                bool byPath = map.isPath(tx + 1, ty) || map.isPath(tx - 1, ty) || map.isPath(tx, ty + 1) || map.isPath(tx, ty - 1);
                if (byPath && world->objectFit(can, tx + 0.5f, ty + 0.5f, 4) == Fences::Fit::Ok) {
                  cx = tx + 0.5f;
                  cy = ty + 0.5f;
                }
              }
          check("a spot for a trash can", cx >= 0);
          if (cx >= 0) {
            world->objectsMutable().add(can, cx, cy, 4);
            world->reindex();
            int canId = world->getObjects().objects().back().id;
            world->objectsMutable().byId(canId)->fill = 13;
            Staff &st = world->getStaff();
            int maintType = -1;
            for (size_t k = 0; k < st.types().size(); k++)
              if (st.types()[k].kind == Staff::Kind::Maint)
                maintType = static_cast<int>(k);
            // (hired on the nearest path tile)
            int worker = -1;
            for (int r = 0; r < 6 && worker < 0 && maintType >= 0; r++)
              for (int dy = -r; dy <= r && worker < 0; dy++)
                for (int dx = -r; dx <= r && worker < 0; dx++)
                  if (map.isPath(static_cast<int>(cx) + dx, static_cast<int>(cy) + dy))
                    worker = st.hire(maintType, static_cast<int>(cx) + dx + 0.5f, static_cast<int>(cy) + dy + 0.5f, map,
                                     world->getFences());
            bool emptying = false;
            for (int i = 0; i < 2000 && worker >= 0; i++) {
              world->update(SDL_GetKeyboardState(nullptr), 0.1f);
              if (const Staff::Member *w = st.member(worker))
                emptying = emptying || w->dutyId == 10503;
              if (world->objectsMutable().byId(canId)->fill == 0)
                break;
            }
            check("a maintenance worker empties the half-full trash can",
                  emptying && world->objectsMutable().byId(canId)->fill == 0);
            // Dung on the grounds (a loose animal's): bagged up ("Cleaning up")
            if (worker >= 0) {
              world->getItems().add(ZooItems::Kind::Dung, -1, cx + 1.3f, cy);
              bool bagging = false;
              for (int i = 0; i < 2000; i++) {
                world->update(SDL_GetKeyboardState(nullptr), 0.1f);
                if (const Staff::Member *w = st.member(worker))
                  bagging = bagging || w->dutyId == 10505;
                if (world->getItems().ofKind(ZooItems::Kind::Dung, -1).empty())
                  break;
              }
              check("a maintenance worker bags dung on the grounds",
                    bagging && world->getItems().ofKind(ZooItems::Kind::Dung, -1).empty());
            }
            if (worker >= 0)
              st.fire(worker);
          }
        }
        // A compost: dung raked pays $50 (Recycling); without one, nothing
        {
          Staff &st = world->getStaff();
          int pen = -1;
          for (const Animals::Member &m : world->getAnimals().members())
            if (m.exhibit >= 0)
              pen = m.exhibit;
          auto rakeOne = [&]() {
            const Animals::Member *z = nullptr;
            for (const Animals::Member &m : world->getAnimals().members())
              if (m.exhibit == pen)
                z = &m;
            if (!z)
              return false;
            world->getItems().add(ZooItems::Kind::Dung, pen, z->x + 0.3f, z->y);
            for (Staff::Member &k : const_cast<std::vector<Staff::Member> &>(st.members()))
              k.workCheck = 0;
            for (int i = 0; i < 3000; i++) {
              world->update(SDL_GetKeyboardState(nullptr), 0.1f);
              if (world->getItems().ofKind(ZooItems::Kind::Dung, pen).empty())
                return true;
            }
            return false;
          };
          st.takeRecycling();
          bool raked0 = pen >= 0 && rakeOne();
          double without = st.takeRecycling();
          world->objectsMutable().add("scenery/building/compost.ai", 3.0f, 3.0f, 4);
          world->reindex();
          bool raked1 = pen >= 0 && rakeOne();
          double with = st.takeRecycling();
          check(("compost: dung raked pays $" + std::to_string(static_cast<int>(with)) + " (none without one: $" +
                 std::to_string(static_cast<int>(without)) + ")").c_str(),
                raked0 && raked1 && without == 0 && with == 50);
        }
        // Research: a program finished - its news, the Completed list - and
        // Tour guide training's effect (cTourGuideBonus 30 -> 40)
        {
          Research &rs = Research::get();
          ResearchBranch *br = rs.branch(0);
          ResearchProgram *cur = br ? rs.current(*br) : nullptr;
          bool finished = false;
          if (cur) {
            br->fundingLevel = static_cast<int>(br->funding.size()) - 1;
            cur->progress = std::max(0.0f, cur->cost - 0.01f);
            Research::Tick rt = rs.advance(3.0f, 1e9);
            for (ResearchProgram *pp : rt.completed)
              finished = finished || pp == cur;
          }
          g_hud->showPanel(14, "");
          g_hud->showTab(14, 4181);
          std::vector<Input> none;
          g_hud->handleInputs(none);
          capture("zoo_research_done");
          UiListBox *rl = dynamic_cast<UiListBox *>(g_hud->getElementById(4164));
          check(("research done: " + (cur ? cur->name : std::string("none")) + ", listed").c_str(),
                finished && cur->done && rl && rl->getItemCount() >= 1);
          g_hud->showPanel(0, "");
          int before = -1, after = -1;
          for (const Staff::Type &t : world->getStaff().types())
            if (t.kind == Staff::Kind::Guide)
              before = t.tourBonus;
          for (ResearchBranch &bb : rs.branches())
            for (ResearchCategory &c : bb.categories)
              for (ResearchProgram &pp : c.programs)
                if (pp.file.find("progex24") != std::string::npos)
                  applyResearch(pp);
          for (const Staff::Type &t : world->getStaff().types())
            if (t.kind == Staff::Kind::Guide)
              after = t.tourBonus;
          check(("Tour guide training: bonus " + std::to_string(before) + " -> " + std::to_string(after)).c_str(),
                before == 30 && after == 40);
          resetResearchEffects();
        }
        // Zoo Status's Commerce Building List: the hot dog stand, its months,
        // visitors and profit; picking it opens its Building Information
        {
          g_hud->showPanel(14, "");
          g_hud->showTab(14, 4180);
          capture("zoo_commerce");
          UiListBox *cl = dynamic_cast<UiListBox *>(g_hud->getElementById(4172));
          int stands = 0;
          for (const PlacedObjects::Object &o : world->getObjects().objects())
            stands += !o.label.empty() && o.price >= 0 ? 1 : 0;
          check(("Commerce Building List: " + std::to_string(cl ? cl->getItemCount() : 0) + " of " + std::to_string(stands)).c_str(),
                cl && stands > 0 && static_cast<int>(cl->getItemCount()) == stands);
          if (cl && cl->getItemCount() > 0) {
            cl->setSelectedIndex(0);
            {
              std::vector<Input> none;
              g_hud->handleInputs(none);
            }
            capture("zoo_commerce_picked");
            check("picking a row opens its Building Information", g_hud->isPanelOpen(21) && g_hud->isPanelOpen(14));
          }
          g_hud->showPanel(0, "");
        }
        // The Animal List and Guest List: everyone, the filters' counts;
        // picking a guest opens its information above the list
        {
          auto textOf = [&](int id) {
            UiText *t = dynamic_cast<UiText *>(g_hud->getElementById(id));
            return t ? t->getText() : std::string();
          };
          std::vector<Input> none;
          g_hud->showPanel(16, "");
          g_hud->handleInputs(none);
          capture("animal_list");
          UiListBox *al = dynamic_cast<UiListBox *>(g_hud->getElementById(3805));
          int animalsNow = static_cast<int>(world->getAnimals().members().size());
          check(("Animal List: " + textOf(3835) + " " + textOf(3816)).c_str(),
                al && static_cast<int>(al->getItemCount()) == animalsNow && textOf(3816) == std::to_string(animalsNow));
          g_hud->showPanel(12, "");
          g_hud->handleInputs(none);
          capture("guest_list");
          UiListBox *gl = dynamic_cast<UiListBox *>(g_hud->getElementById(3705));
          int inPark = 0;
          for (const Guests::Guest &g : gs.guests())
            inPark += g.x > -999 ? 1 : 0;
          check(("Guest List: " + textOf(3727) + " " + textOf(3716)).c_str(),
                gl && static_cast<int>(gl->getItemCount()) == inPark && textOf(3716) == std::to_string(inPark));
          if (gl && gl->getItemCount() > 0) {
            gl->setSelectedIndex(0);
            g_hud->handleInputs(none);
            capture("guest_list_picked");
            check("picking a guest opens Guest Information above the list", g_hud->isPanelOpen(9) && g_hud->isPanelOpen(12));
          }
          g_hud->showPanel(0, "");
        }
        // The Message List (the Messages button): the zoo's news kept,
        // newest at the top
        {
          g_hud->postMessage("Test message one.");
          g_hud->postMessage("Test message two.", 2);
          int about = world->getAnimals().members().empty() ? -1 : world->getAnimals().members().front().id;
          if (about >= 0)
            g_hud->postMessage(world->getAnimals().member(about)->name + " is not happy.", 2,
                               UiGameScreen::Subject::Animal, about);
          g_hud->postMessage("Test message one."); // (again within a minute: dropped)
          click(1006);
          std::vector<Input> none;
          g_hud->handleInputs(none);
          capture("message_list");
          if (about >= 0) {
            UiListBox *ml0 = dynamic_cast<UiListBox *>(g_hud->getElementById(7201));
            if (ml0)
              ml0->setSelectedIndex(ml0->getItemCount() == 3 ? 0 : 1);
            g_hud->handleInputs(none);
            check("clicking news about an animal opens it", g_hud->isPanelOpen(6));
          }
          UiListBox *ml = dynamic_cast<UiListBox *>(g_hud->getElementById(7201));
          check(("Message List open: " + std::to_string(ml ? ml->getItemCount() : 0) + " messages").c_str(),
                g_hud->isPanelOpen(29) && ml && ml->getItemCount() >= 3);
          click(1006);
          g_hud->handleInputs(none);
          check("and closed again", !g_hud->isPanelOpen(29));
        }
        // The freeform goals (awards.scn, donation.scn, unlock.scn): the
        // adopt lock (animals' average happiness under 25: the Animals tab
        // greyed and a popup; 45 or more: back), and an award (its popup,
        // then listed on Zoo Awards)
        {
          startGoals(rm, chosen->path);
          check(("freeform goals loaded: " + std::to_string(g_goals.all().size())).c_str(), g_goals.all().size() > 20);
          Animals &an = world->getAnimals();
          std::vector<float> was;
          for (const Animals::Member &m : an.members()) {
            was.push_back(m.happiness);
            an.member(m.id)->happiness = -100;
          }
          g_goals.evaluateAll();
          UiElement *tab = g_hud->getElementById(2075);
          bool locked = tab && tab->isDisabled();
          capture("goal_adopt_lock");
          std::vector<Input> none;
          for (int k = 0; k < 4; k++)
            g_hud->handleInputs(none), g_hud->closeTopmost();
          size_t i = 0;
          for (const Animals::Member &m : an.members())
            an.member(m.id)->happiness = 100;
          g_goals.evaluateAll();
          bool unlocked = tab && !tab->isDisabled();
          i = 0;
          for (const Animals::Member &m : an.members())
            an.member(m.id)->happiness = was[i++];
          check("adopt lock: greyed under 25, back at 45", locked && unlocked);
          for (Goals::Goal &gl : g_goals.mutableAll())
            if (gl.name == "best_exhibit1" || gl.name == "best_exhibit1_popup")
              gl.value = 0;
          g_goals.evaluateAll();
          capture("goal_award_popup");
          for (int k = 0; k < 4; k++)
            g_hud->closeTopmost();
          g_hud->showPanel(14, "");
          g_hud->showTab(14, 4105);
          g_hud->handleInputs(none);
          capture("zoo_awards_won");
          UiListBox *al = dynamic_cast<UiListBox *>(g_hud->getElementById(4124));
          check(("award 1 received and listed (" + std::to_string(al ? al->getItemCount() : 0) + ")").c_str(),
                g_goals.awards.size() == 1 && g_goals.awards[0] == 1 && al && al->getItemCount() == 1);
          g_hud->showPanel(0, "");
          // A species' first young brings a donation (panda_donate: $50,000
          // and its popup) - tried with the zebra standing in for the panda
          {
            Animals &zoo = world->getAnimals();
            const Animals::Member *z = zoo.members().empty() ? nullptr : &zoo.members().front();
            if (z) {
              int kind = z->baby ? 3 : z->female ? 1 : 0;
              for (Goals::Goal &gl : g_goals.mutableAll())
                if (gl.name == "panda_donate" || gl.name == "panda_donate_popup") {
                  gl.arga = zoo.types()[z->type].nameId;
                  gl.argb = kind;
                }
              while (g_hud->dialogOpen())
                g_hud->closeTopmost();
              double cash0 = g_sim.cash();
              g_goals.evaluateAll();
              bool popped = g_hud->dialogOpen();
              capture("goal_donation_popup");
              check(("a species' young: a donation of $" + std::to_string(static_cast<long>(g_sim.cash() - cash0)) + " and its popup").c_str(),
                    std::lround(g_sim.cash() - cash0) == 50000 && popped);
            }
          }
          // (the popups read: the game goes on)
          while (g_hud->dialogOpen())
            g_hud->closeTopmost();
          check("a popup with pause=1 paused the game until read", g_popupPaused || !world->isPaused());
          resumeAfterPopup();
        }
        // Holding the fence tool, a click on a window's X closes the window
        // and lays nothing (the click doesn't fall through to the map)
        {
          Fences &fn = world->getFences();
          world->getAnimals().takeMessages(); // (their notices would sit on top)
          world->setFenceTool(fn.typeIndex("chainlnk"));
          g_hud->showPanel(14, "");
          std::vector<Input> none;
          gameInputs(renderer, rm, none);
          size_t pieces = fn.pieces().size();
          UiElement *x = g_hud->getElementById(4134);
          bool closed = false;
          if (x) {
            SDL_Rect r = x->getLastRect();
            UiTransform ut = getUiTransform(renderer);
            Input in{};
            in.type = InputType::POSITIONED;
            in.event = InputEvent::LEFT_CLICK;
            in.x = static_cast<int>((r.x + r.w / 2) * ut.scale + ut.offsetX);
            in.y = static_cast<int>((r.y + r.h / 2) * ut.scale + ut.offsetY);
            in.position = {in.x, in.y};
            Input up = in;
            up.event = InputEvent::LEFT_RELEASE;
            gameInputs(renderer, rm, {in});
            gameInputs(renderer, rm, {up});
            closed = !g_hud->isPanelOpen(14);
          }
          world->setFenceTool(-1);
          while (g_hud->hasDialog())
            g_hud->closeTopmost();
          check("clicking a window's X with a tool held: closed, no fence laid", closed && fn.pieces().size() == pieces);
        }
        // Snap Shot: screenshots/Zoo###.jpg and "Wrote screenshot in ..."
        {
          click(1099);
          bool asked = g_snapshot == 1;
          g_snapshot = 0;
          std::filesystem::path dir = std::filesystem::path(SaveGame::folder()).parent_path() / "screenshots";
          std::set<std::string> before;
          std::error_code ec;
          for (const auto &e : std::filesystem::directory_iterator(dir, ec))
            before.insert(e.path().string());
          SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
          SDL_RenderClear(renderer);
          world->draw(renderer);
          takeSnapshot(renderer, rm);
          std::string made;
          for (const auto &e : std::filesystem::directory_iterator(dir, ec))
            if (!before.count(e.path().string()))
              made = e.path().string();
          check(("Snap Shot wrote " + std::filesystem::path(made).filename().string()).c_str(),
                asked && !made.empty() && std::filesystem::file_size(made, ec) > 1000);
          if (!made.empty())
            std::filesystem::remove(made, ec);
        }
        // Undo (the Single_Undo button): a fence laid, then undone - the
        // pieces gone and the money back; the button greyed again
        {
          Fences &fn = world->getFences();
          int fx = -1, fy = -1;
          for (int y = 30; y < 60 && fx < 0; y++)
            for (int x = 10; x < 60 && fx < 0; x++) {
              Fences::Edge a{true, x, y}, b{true, x + 1, y};
              if (fn.canPlace(a, world->getMap()) && fn.canPlace(b, world->getMap()) && !fn.at(a) && !fn.at(b)) {
                fx = x;
                fy = y;
              }
            }
          check("open ground for a fence to undo", fx >= 0);
          if (fx >= 0) {
            double before = g_sim.cash();
            world->setFenceTool(fn.typeIndex("chainlnk"));
            int x0, y0, x1, y1;
            world->vertexToWindow(fx, fy, x0, y0);
            world->vertexToWindow(fx + 2, fy, x1, y1);
            handleToolResult(world->mouseDown(x0, y0), rm);
            world->mouseMove(x1, y1);
            handleToolResult(world->mouseUp(x1, y1), rm);
            world->setFenceTool(-1);
            bool laid = fn.at(Fences::Edge{true, fx, fy}) && fn.at(Fences::Edge{true, fx + 1, fy});
            double paid = before - g_sim.cash();
            std::vector<Input> none;
            world->getAnimals().takeMessages(); // (their notices would sit on top)
            worldInputs(renderer, rm, none);
            UiElement *ub = g_hud->getElementById(1075);
            bool lit = ub && !ub->isDisabled();
            double atUndo = g_sim.cash();
            click(1075);
            double back = g_sim.cash() - atUndo;
            world->getAnimals().takeMessages();
            worldInputs(renderer, rm, none);
            bool gone = !fn.at(Fences::Edge{true, fx, fy}) && !fn.at(Fences::Edge{true, fx + 1, fy});
            check(("Undo: a fence laid ($" + std::to_string(static_cast<int>(paid)) + "), undone, the money back").c_str(),
                  laid && paid > 0 && lit && gone && std::fabs(back - paid) < 0.01 && ub->isDisabled());
          }
        }
        // The view toggles by the map (Hide Foliage, Hide Buildings, Hide
        // Guests): trees, buildings and guests go (staff and animals stay),
        // and come back
        {
          click(1066);
          click(1067);
          click(1068);
          {
            // (the animals' news held back: its notice would sit on top)
            world->getAnimals().takeMessages();
            std::vector<Input> none;
            worldInputs(renderer, rm, none);
          }
          capture("view_hidden");
          const PlacedObjects &po = world->getObjects();
          check("Hide Foliage / Buildings / Guests", po.hideFoliage && po.hideBuildings && gs.hideAll);
          click(1066);
          click(1067);
          click(1068);
          {
            // (the animals' news held back: its notice would sit on top)
            world->getAnimals().takeMessages();
            std::vector<Input> none;
            worldInputs(renderer, rm, none);
          }
          check("and shown again", !po.hideFoliage && !po.hideBuildings && !gs.hideAll);
        }
      }
    }
  }

  // Terraforming (zoo.exe's terrain tool): savannah painted with the 2 x 2
  // brush, charged on Accept by its tiles ($70 each); on flat ground a hill
  // raised 2 units with the 2 x 2 brush costs $384 (as in the original),
  // and Undo puts the ground back
  {
    WorldMap &map = world->getMap();
    Fences &fn = world->getFences();
    TerrainTool &tt = world->getTerrainTool();
    // Open zoo ground, 13 x 13 with no fence or exhibit in it
    int cx = -1, cy = -1;
    for (int r = 0; r < 30 && cx < 0; r++)
      for (int dy = -r; dy <= r && cx < 0; dy++)
        for (int dx = -r; dx <= r && cx < 0; dx++) {
          int x0 = testSpotX + dx, y0 = testSpotY + dy;
          bool ok = true;
          for (int y = y0 - 6; y <= y0 + 6 && ok; y++)
            for (int x = x0 - 6; x <= x0 + 6 && ok; x++) {
              const MapTile *t = map.getTile(x, y);
              ok = t && fn.insideZoo(x, y) && fn.exhibitAt(x, y) < 0 && !map.isPath(x, y) &&
                   t->terrainType != 9 && t->terrainType != 10 &&
                   !fn.at(Fences::Edge{false, x, y}) && !fn.at(Fences::Edge{true, x, y});
            }
          if (ok) {
            cx = x0;
            cy = y0;
          }
        }
    check("open ground to terraform", cx >= 0);
    if (cx >= 0) {
      // Flat and clear for the test (put back after): its rocks lifted
      // (they keep the ground under them from moving)
      std::vector<PlacedObjects::Object> lifted;
      for (int i = static_cast<int>(world->getObjects().objects().size()) - 1; i >= 0; i--) {
        const PlacedObjects::Object &o = world->getObjects().objects()[i];
        if (!o.fence && std::fabs(o.x - cx) < 7 && std::fabs(o.y - cy) < 7) {
          lifted.push_back(o);
          world->objectsMutable().remove(i);
        }
      }
      world->reindex();
      std::map<std::pair<int, int>, MapTile> saved;
      int flat = map.getTile(cx, cy)->cornerHeight[0];
      for (int y = cy - 6; y <= cy + 6; y++)
        for (int x = cx - 6; x <= cx + 6; x++) {
          MapTile *t = map.getTileMutable(x, y);
          saved[{x, y}] = *t;
          for (int c = 0; c < 4; c++)
            t->cornerHeight[c] = flat;
          t->height = flat;
          t->terrainType = 0;
        }
      map.touch();
      world->centreOn(cx + 0.5f, cy + 0.5f);
      auto at = [&](int x, int y, int &px, int &py) {
        world->pointToWindow(x + 0.5f, y + 0.5f, static_cast<float>(map.getTile(x, y)->cornerHeight[0]), px, py);
      };
      int px, py;
      // Painting
      world->setTerrainTool(true, true, 1, 2, 0);
      at(cx, cy, px, py);
      world->mouseMove(px, py);
      capture("terrain_brush");
      int stroke = world->hoverCost();
      world->mouseDown(px, py);
      int px2, py2;
      at(cx + 2, cy, px2, py2);
      world->mouseMove(px2, py2);
      world->mouseUp(px2, py2);
      capture("terrain_painted");
      int painted = 0;
      for (int y = cy - 6; y <= cy + 6; y++)
        for (int x = cx - 6; x <= cx + 6; x++)
          painted += map.getTile(x, y)->terrainType == 1 ? 1 : 0;
      float paintCost = tt.cost();
      check(("savannah painted: " + std::to_string(painted) + " tiles, $" + std::to_string((int)paintCost) +
             " to pay (a stroke $" + std::to_string(stroke) + ")").c_str(),
            painted >= 4 && paintCost == painted * 70.0f && stroke == 4 * 70);
      int charge = tt.accept();
      check("Accept charges it and starts again", charge == painted * 70 && tt.cost() == 0);
      // A hill: pressed and dragged 2 units up (16 px a unit)
      world->setTerrainTool(false, true, 1, 2, 0);
      world->setTerrainTool(true, false, 0, 2, 0);
      at(cx, cy, px, py);
      world->mouseMove(px, py);
      world->mouseDown(px, py);
      float scale = getUiTransform(renderer).scale;
      for (int i = 1; i <= 4; i++)
        world->mouseMove(px, py - static_cast<int>(std::lround(8 * i * scale)));
      world->mouseUp(px, py - static_cast<int>(std::lround(32 * scale)));
      world->hoverOff();
      capture("terrain_hill");
      int top = map.getTile(cx, cy)->cornerHeight[CORNER_X1Y1];
      check(("hill raised 2 units for $" + std::to_string((int)tt.cost()) + " (top " + std::to_string(top - flat) + ")").c_str(),
            top - flat == 2 && (int)tt.cost() == 384);
      tt.undo();
      bool back = true;
      for (int y = cy - 6; y <= cy + 6; y++)
        for (int x = cx - 6; x <= cx + 6; x++)
          for (int c = 0; c < 4; c++)
            back = back && map.getTile(x, y)->cornerHeight[c] == flat;
      check("Undo puts the ground back", back && tt.cost() == 0);
      // Cliffs: the brush alone
      world->setTerrainTool(false, false, 0, 2, 0);
      world->setTerrainTool(true, false, 0, 2, 1);
      world->mouseMove(px, py);
      world->mouseDown(px, py);
      world->mouseMove(px, py - static_cast<int>(std::lround(18 * scale)));
      world->mouseUp(px, py - static_cast<int>(std::lround(18 * scale)));
      world->hoverOff();
      capture("terrain_cliff");
      check("a cliff: the brush up a unit, the ground beside it not",
            map.getTile(cx, cy)->cornerHeight[0] == flat + 1 && map.getTile(cx - 1, cy)->cornerHeight[CORNER_X1Y0] == flat);
      tt.undo();
      world->setTerrainTool(false, true, 0, 2, 0);
      for (const PlacedObjects::Object &o : lifted)
        world->objectsMutable().restore(o);
      world->reindex();
      for (auto &[pos, t] : saved)
        *map.getTileMutable(pos.first, pos.second) = t;
      map.touch();
    }
  }

  // Ten minutes of zoo life at once: nothing breaks, the keepers keep the
  // animals fed, guests come and go
  {
    Animals &an = world->getAnimals();
    float worstHunger = 0;
    int maxGuests = 0;
    Uint64 s0 = SDL_GetPerformanceCounter();
    for (int i = 0; i < 6000; i++) {
      world->update(SDL_GetKeyboardState(nullptr), 0.1f);
      int n = 0;
      world->getGuests().takeIncome(n);
      world->getGuests().takeConcessions();
      world->getStaff().takeUpkeep();
      an.takeNotices();
      an.takeMessages();
      maxGuests = std::max(maxGuests, static_cast<int>(world->getGuests().guests().size()));
      if (i > 3000)
        for (const Animals::Member &m : an.members())
          if (m.exhibit >= 0)
            worstHunger = std::max(worstHunger, m.hunger);
    }
    double secs = (SDL_GetPerformanceCounter() - s0) / static_cast<double>(SDL_GetPerformanceFrequency());
    int fed = 0;
    for (const Animals::Member &m : an.members())
      fed += m.exhibit >= 0 && m.hunger < 100 ? 1 : 0;
    check(("10 minutes of play (" + std::to_string(secs).substr(0, 4) + " s): " + std::to_string(an.members().size()) +
           " animals, worst hunger " + std::to_string(static_cast<int>(worstHunger)) + ", up to " +
           std::to_string(maxGuests) + " guests").c_str(),
          worstHunger < 100 && maxGuests > 0);
    capture("soak_end");
  }

  // A shelter: a lean-to in the zebra's exhibit; the zebra goes in (out of
  // sight for its cTimeInside) and comes out again. Then old age: its bDie
  // played, then gone ("... has died of old age.")
  {
    Animals &an = world->getAnimals();
    Animals::Member *z = nullptr;
    for (const Animals::Member &m : an.members())
      if (m.exhibit >= 0)
        z = an.member(m.id);
    if (z) {
      int zid = z->id, pen = z->exhibit;
      std::string shelter = "scenery/other/leanto1.ai";
      bool put = false;
      const Fences::Exhibit *ex = world->getFences().exhibit(pen);
      for (auto [x, y] : ex->tiles) {
        if (put)
          break;
        if (world->objectFit(shelter, x + 1.0f, y + 0.5f, 4) == Fences::Fit::Ok)
          put = world->objectsMutable().add(shelter, x + 1.0f, y + 0.5f, 4);
      }
      world->reindex();
      check("a lean-to in the zebra's exhibit", put);
      // (the zebra moved off it if it's under it: to the open tile furthest away)
      if (put) {
        const PlacedObjects::Object &lean = world->getObjects().objects().back();
        float far = -1;
        for (auto [tx, ty] : ex->tiles)
          if (an.canPlace(z->type, tx + 0.5f, ty + 0.5f, world->getMap(), world->getFences()) == Fences::Fit::Ok) {
            float d = std::hypot(tx + 0.5f - lean.x, ty + 0.5f - lean.y);
            if (d > far) {
              far = d;
              z->x = tx + 0.5f;
              z->y = ty + 0.5f;
            }
          }
        z->path.clear();
        z->pathAt = 0;
      }
      auto &type = const_cast<Animals::Type &>(an.types()[z->type]);
      int savedChance = type.buildingUseChance;
      type.buildingUseChance = 100;
      z->buildingCheck = 0;
      bool wentIn = false, cameOut = false;
      for (int i = 0; i < 1200; i++) {
        world->update(SDL_GetKeyboardState(nullptr), 0.1f);
        const Animals::Member *zz = an.member(zid);
        if (!zz)
          break;
        if (zz->inside)
          wentIn = true;
        if (wentIn && !zz->inside) {
          cameOut = true;
          break;
        }
      }
      type.buildingUseChance = savedChance;
      check("the zebra goes into the shelter and comes out", wentIn && cameOut);
      // A big shelter (the elephant house) against the pen's fence, from all
      // four sides: the fence behind it goes behind it, the fence in front
      // in front (drawn in strips, not as one)
      {
        std::string house = "scenery/building/elehous1.ai";
        int hid = -1;
        // (outside the pen, as close to its fence as it fits)
        float pcx = 0, pcy = 0;
        for (auto [tx, ty] : ex->tiles) {
          pcx += tx + 0.5f;
          pcy += ty + 0.5f;
        }
        pcx /= ex->tiles.size();
        pcy /= ex->tiles.size();
        float bestD = 1e9f, bx = 0, by = 0;
        for (int dy = -10; dy <= 10; dy++)
          for (int dx = -10; dx <= 10; dx++)
            for (float off : {0.0f, 0.5f}) {
              float x = std::floor(pcx) + dx + off, y = std::floor(pcy) + dy + off;
              float d = std::hypot(x - pcx, y - pcy);
              if (d < bestD && world->objectFit(house, x, y, 4) == Fences::Fit::Ok) {
                bestD = d;
                bx = x;
                by = y;
              }
            }
        if (bestD < 1e9f && world->objectsMutable().add(house, bx, by, 4))
          hid = world->getObjects().objects().back().id;
        world->reindex();
        if (hid >= 0) {
          const PlacedObjects::Object *ho = world->objectsMutable().byId(hid);
          world->centreOn(ho->x, ho->y);
          for (int r = 0; r < 4; r++) {
            capture(("house_fence_rot" + std::to_string(r)).c_str());
            world->rotateView(1);
          }
          // (its footprint against its art at each facing)
          world->debugFootprints = true;
          for (int f = 0; f < 8; f += 2) {
            world->objectsMutable().byId(hid)->facing = f;
            capture(("house_facing" + std::to_string(f)).c_str());
          }
          world->debugFootprints = false;
          world->objectsMutable().byId(hid)->facing = 4;
          // A fence across it: not laid (none can go through a building)
          {
            const PlacedObjects::Object *h2 = world->objectsMutable().byId(hid);
            Fences::Edge through{true, static_cast<int>(std::floor(h2->x)), static_cast<int>(std::lround(h2->y))};
            Fences::Edge across{false, static_cast<int>(std::lround(h2->x)), static_cast<int>(std::floor(h2->y))};
            check("no fence through a building",
                  !world->getFences().canPlace(through, world->getMap()) && !world->getFences().canPlace(across, world->getMap()));
          }
          int i = world->getObjects().indexOf(hid);
          world->objectsMutable().remove(i);
          world->reindex();
        }
        check("an elephant house against the fence, four views", hid >= 0);
        // And a fence line along its front: from two sides the fence is in
        // front of it, and goes over all of its wall (a piece sorted as a
        // whole came over every other strip of it)
        {
          Fences &fl = world->getFences();
          int line = fl.typeIndex("chainlnk");
          hid = -1;
          int hx = -1, hy = -1;
          // (centred so its front, +y, is on a tile edge: the fence's row)
          auto [fpx, fpy] = world->footprint(house);
          float offX = (fpx % 4) ? 0.5f : 0.0f, offY = (fpy % 4) ? 0.5f : 0.0f;
          int front = static_cast<int>(std::lround(offY + fpy / 4.0f));
          for (int dy = -12; dy <= 12 && hid < 0; dy++)
            for (int dx = -12; dx <= 12 && hid < 0; dx++) {
              int x = static_cast<int>(pcx) + dx, y = static_cast<int>(pcy) + dy;
              if (world->objectFit(house, x + offX, y + offY, 4) != Fences::Fit::Ok)
                continue;
              bool free = true;
              for (int k = -2; k < 2; k++)
                free = free && fl.canPlace(Fences::Edge{true, x + k, y + front}, world->getMap());
              if (!free)
                continue;
              // (the fence first, then the house against it)
              for (int k = -2; k < 2; k++)
                fl.place(Fences::Edge{true, x + k, y + front}, line, 0, world->getMap());
              world->reindex();
              if (world->objectFit(house, x + offX, y + offY, 4) != Fences::Fit::Ok ||
                  !world->objectsMutable().add(house, x + offX, y + offY, 4)) {
                for (int k = -2; k < 2; k++)
                  fl.remove(Fences::Edge{true, x + k, y + front}, world->getMap());
                world->reindex();
                continue;
              }
              hid = world->getObjects().objects().back().id;
              hx = x;
              hy = y;
            }
          if (hid >= 0) {
            world->reindex();
            world->centreOn(static_cast<float>(hx), static_cast<float>(hy));
            for (int r = 0; r < 4; r++) {
              capture(("house_fenceline_rot" + std::to_string(r)).c_str());
              world->rotateView(1);
            }
            for (int k = -2; k < 2; k++)
              fl.remove(Fences::Edge{true, hx + k, hy + front}, world->getMap());
            world->objectsMutable().remove(world->getObjects().indexOf(hid));
            world->reindex();
          }
          check("an elephant house with a fence along its front, four views", hid >= 0);
        }
      }
      // (the lean-to taken away again: the pen is small)
      if (put) {
        for (int i = static_cast<int>(world->getObjects().objects().size()) - 1; i >= 0; i--)
          if (world->getObjects().objects()[i].typeName == "leanto1") {
            world->objectsMutable().remove(i);
            break;
          }
        world->reindex();
      }
      // Water: a corner of its exhibit fresh water - the zebra (no swimmer)
      // drinks at its edge, never in it; a hippo goes in and swims
      {
        WorldMap &wmap = world->getMap();
        std::vector<std::pair<std::pair<int, int>, int>> was;
        // (the part of the exhibit the zebra can walk to: no fence or cliff
        // between)
        std::vector<std::pair<int, int>> pen;
        {
          const Animals::Member *z0 = an.member(zid);
          std::set<std::pair<int, int>> seen;
          std::vector<std::pair<int, int>> open;
          if (z0) {
            open.push_back({static_cast<int>(std::floor(z0->x)), static_cast<int>(std::floor(z0->y))});
            seen.insert(open.back());
          }
          while (!open.empty()) {
            auto [x, y] = open.back();
            open.pop_back();
            pen.push_back({x, y});
            const int d4[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
            for (auto &dd : d4) {
              int nx = x + dd[0], ny = y + dd[1];
              if (seen.count({nx, ny}) || !ex->tiles.count({nx, ny}))
                continue;
              Fences::Edge e = nx != x ? Fences::Edge{false, std::max(x, nx), y} : Fences::Edge{true, x, std::max(y, ny)};
              const MapTile *a0 = wmap.getTile(x, y), *b0 = wmap.getTile(nx, ny);
              bool flatJoin = a0 && b0;
              if (flatJoin) {
                int ac[2], bc[2];
                if (nx > x) { ac[0] = a0->cornerHeight[CORNER_X1Y0]; ac[1] = a0->cornerHeight[CORNER_X1Y1]; bc[0] = b0->cornerHeight[CORNER_X0Y0]; bc[1] = b0->cornerHeight[CORNER_X0Y1]; }
                else if (nx < x) { ac[0] = a0->cornerHeight[CORNER_X0Y0]; ac[1] = a0->cornerHeight[CORNER_X0Y1]; bc[0] = b0->cornerHeight[CORNER_X1Y0]; bc[1] = b0->cornerHeight[CORNER_X1Y1]; }
                else if (ny > y) { ac[0] = a0->cornerHeight[CORNER_X0Y1]; ac[1] = a0->cornerHeight[CORNER_X1Y1]; bc[0] = b0->cornerHeight[CORNER_X0Y0]; bc[1] = b0->cornerHeight[CORNER_X1Y0]; }
                else { ac[0] = a0->cornerHeight[CORNER_X0Y0]; ac[1] = a0->cornerHeight[CORNER_X1Y0]; bc[0] = b0->cornerHeight[CORNER_X0Y1]; bc[1] = b0->cornerHeight[CORNER_X1Y1]; }
                flatJoin = ac[0] == bc[0] && ac[1] == bc[1];
              }
              if (!flatJoin || world->getFences().at(e) || world->getIndex().blocked(nx, ny))
                continue;
              seen.insert({nx, ny});
              open.push_back({nx, ny});
            }
          }
          // (the water away from where the zebra stands: the farthest three)
          if (z0)
            std::sort(pen.begin(), pen.end(), [&](const std::pair<int, int> &a, const std::pair<int, int> &b) {
              return std::hypot(a.first + 0.5f - z0->x, a.second + 0.5f - z0->y) > std::hypot(b.first + 0.5f - z0->x, b.second + 0.5f - z0->y);
            });
        }
        check(("water test: " + std::to_string(pen.size()) + " reachable tiles").c_str(), pen.size() >= 5);
        for (size_t k = 0; k < pen.size() && k < 3; k++) {
          MapTile *tile = wmap.getTileMutable(pen[k].first, pen[k].second);
          was.push_back({pen[k], tile->terrainType});
          tile->terrainType = 9;
        }
        wmap.touch();
        world->reindex();
        Animals::Member *zw = an.member(zid);
        if (zw) {
          // (moved off the water if it's on it)
          for (auto [tx, ty] : pen)
            if (!Animals::isWater(wmap, static_cast<int>(zw->x), static_cast<int>(zw->y)))
              break;
            else if (an.canPlace(zw->type, tx + 0.5f, ty + 0.5f, wmap, world->getFences()) == Fences::Fit::Ok) {
              zw->x = tx + 0.5f;
              zw->y = ty + 0.5f;
            }
          auto &zt = const_cast<Animals::Type &>(an.types()[zw->type]);
          int savedDrink = zt.drinkWaterChance, savedChase = zt.chaseAnimalChance;
          zt.drinkWaterChance = 100;
          zt.chaseAnimalChance = 0;
          zw->boredCheck = 1;
          zw->hunger = 0;
          zw->stack.clear();
          bool drank = false, wet = false;
          for (int i = 0; i < 1500 && an.member(zid); i++) {
            world->update(SDL_GetKeyboardState(nullptr), 0.1f);
            const Animals::Member *zz = an.member(zid);
            wet = wet || Animals::isWater(wmap, static_cast<int>(std::floor(zz->x)), static_cast<int>(std::floor(zz->y)));
            if (zz->drinkX >= 0 && zz->anim == "eat" && std::hypot(zz->drinkX - zz->x, zz->drinkY - zz->y) < 1.2f) {
              drank = true;
              world->centreOn(zz->x, zz->y);
              capture("zebra_drinking");
              break;
            }
          }
          zt.drinkWaterChance = savedDrink;
          zt.chaseAnimalChance = savedChase;
          check("a zebra drinks at the water's edge, never in it", drank && !wet);
        }
        int hippoType = an.typeOfFile("animals/hippo.ai");
        int hippo = -1;
        for (auto [tx, ty] : pen)
          if (hippo < 0 && hippoType >= 0 && !Animals::isWater(wmap, tx, ty))
            hippo = an.adopt(hippoType, false, tx + 0.5f, ty + 0.5f, wmap, world->getFences());
        bool swam = false;
        if (hippo >= 0) {
          auto &ht = const_cast<Animals::Type &>(an.types()[hippoType]);
          int savedEnter = ht.enterWaterChance, savedNeed = ht.waterNeeded;
          ht.enterWaterChance = 100;
          ht.waterNeeded = 0;
          an.member(hippo)->waterCheck = 1;
          for (int i = 0; i < 1500 && an.member(hippo); i++) {
            world->update(SDL_GetKeyboardState(nullptr), 0.1f);
            const Animals::Member *h = an.member(hippo);
            if (h->waterMode && Animals::isWater(wmap, static_cast<int>(std::floor(h->x)), static_cast<int>(std::floor(h->y)))) {
              swam = true;
              world->centreOn(h->x, h->y);
              capture("hippo_swimming");
              break;
            }
          }
          ht.enterWaterChance = savedEnter;
          ht.waterNeeded = savedNeed;
          an.remove(hippo);
        }
        check("a hippo goes into the water and swims", swam);
        for (auto &[pos, type] : was)
          wmap.getTileMutable(pos.first, pos.second)->terrainType = type;
        wmap.touch();
        world->reindex();
      }
      Guests &gs = world->getGuests();
      // A black bear (a jumper, bash strength 40) and one low fence piece:
      // it jumps out, the piece breaks as it lands, a man nearby runs
      {
        Fences &fnc = world->getFences();
        int bearType = an.typeOfFile("animals/blackbr.ai");
        // (a piece on the pen's edge, with open zoo ground beyond)
        Fences::Edge low{false, -1, -1};
        int oldType = -1;
        for (auto [tx, ty] : ex->tiles) {
          const int d4[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
          for (auto &dd : d4) {
            int nx = tx + dd[0], ny = ty + dd[1];
            if (low.x >= 0 || ex->tiles.count({nx, ny}) || fnc.exhibitAt(nx, ny) >= 0 || !fnc.insideZoo(nx, ny))
              continue;
            Fences::Edge e = dd[0] ? Fences::Edge{false, std::max(tx, nx), ty} : Fences::Edge{true, tx, std::max(ty, ny)};
            const Fences::Piece *pc = fnc.at(e);
            if (pc && !pc->gate) {
              low = e;
              oldType = pc->type;
            }
          }
        }
        int bear = -1;
        if (low.x >= 0 && bearType >= 0) {
          fnc.setPieceType(low, fnc.typeIndex("smchain"));
          for (auto [tx, ty] : ex->tiles)
            if (bear < 0)
              bear = an.adopt(bearType, false, tx + 0.5f, ty + 0.5f, world->getMap(), fnc);
        }
        bool out = false, broke = false, ran = false, mauled = false;
        // (the pen's zebra kept out of it: a bear hunts zebras)
        if (Animals::Member *zp = an.member(zid))
          zp->claimedBy = 999;
        // (the keepers held back: they'd dart and crate it at once)
        for (Staff::Member &k : const_cast<std::vector<Staff::Member> &>(world->getStaff().members()))
          k.workCheck = 1e9f;
        for (int i = 0; i < 3000 && bear >= 0; i++) {
          world->update(SDL_GetKeyboardState(nullptr), 0.1f);
          Animals::Member *b = an.member(bear);
          if (!b)
            break;
          if (!out && b->escaped) {
            out = true;
            // (a guest set down beside it on the nearest path)
            float bx = b->x, by = b->y, bestD = 1e9f;
            int px = -1, py = -1;
            for (int dy = -6; dy <= 6; dy++)
              for (int dx = -6; dx <= 6; dx++)
                if (world->getMap().isPath(static_cast<int>(bx) + dx, static_cast<int>(by) + dy) &&
                    std::hypot(dx, dy) < bestD) {
                  bestD = std::hypot(dx, dy);
                  px = static_cast<int>(bx) + dx;
                  py = static_cast<int>(by) + dy;
                }
            // (a man among them: guests come in until one does)
            auto hasMan = [&] {
              for (const Guests::Guest &g : gs.guests())
                if (g.x > -999 && gs.types()[g.type].kind == Guests::Kind::Man)
                  return true;
              return false;
            };
            for (int k = 0; k < 40 && !hasMan(); k++)
              gs.admit(0);
            if (px >= 0)
              for (const Guests::Guest &g : gs.guests())
                if (g.x > -999 && gs.types()[g.type].kind == Guests::Kind::Man) {
                  Guests::Guest &gg = const_cast<Guests::Guest &>(g);
                  gg.x = px + 0.5f;
                  gg.y = py + 0.5f;
                  gg.path.clear();
                  gg.pathAt = 0;
                  gg.chaseCheck = 0;
                  break;
                }
          }
          broke = broke || fnc.broken(low);
          for (const Guests::Guest &g : gs.guests()) {
            ran = ran || g.state == Guests::State::Fleeing;
            if (g.state == Guests::State::Caught && !mauled) {
              mauled = true;
              world->centreOn(g.x, g.y);
              capture("bear_mauls_guest");
            }
          }
          if (out && broke && ran && mauled)
            break;
        }
        check(("black bear: out " + std::to_string(out) + ", fence broken " + std::to_string(broke) + ", guests ran " +
               std::to_string(ran)).c_str(), out && broke && ran);
        check("the loose bear chases a man down and mauls him", mauled);
        // Darted mid-chase: the chase is over, nobody mauled (it had caught
        // him after the dart, hidden for the mauling)
        {
          bool chased = false, spared = true;
          Animals::Member *b = bear >= 0 ? an.member(bear) : nullptr;
          if (b && b->escaped) {
            b->maulFor = 0;
            b->prey = -1;
            b->preyCheck = 0;
            const Guests::Guest *man = nullptr;
            for (int k = 0; k < 60 && !man; k++) {
              for (const Guests::Guest &g : gs.guests())
                if (!man && g.x > -999 && !g.attacked && g.state != Guests::State::Caught &&
                    gs.types()[g.type].kind == Guests::Kind::Man)
                  man = &g;
              if (!man)
                gs.admit(0);
            }
            if (man) {
              Guests::Guest &gg = const_cast<Guests::Guest &>(*man);
              int manId = gg.id;
              gg.x = b->x + 1.5f;
              gg.y = b->y;
              gg.path.clear();
              gg.pathAt = 0;
              for (int i = 0; i < 100 && !chased; i++) {
                world->update(SDL_GetKeyboardState(nullptr), 0.1f);
                b = an.member(bear);
                if (b && b->prey >= 0) {
                  chased = true;
                  an.tranquilise(bear);
                }
              }
              for (int i = 0; i < 150 && chased; i++) {
                world->update(SDL_GetKeyboardState(nullptr), 0.1f);
                if (const Guests::Guest *g = gs.guest(manId))
                  spared = spared && g->state != Guests::State::Caught;
              }
              b = an.member(bear);
              spared = spared && b && b->prey < 0 && b->maulFor <= 0;
            }
          }
          check("a bear darted mid-chase mauls nobody", chased && spared);
        }
        // A komodo dragon (it can't jump, climb or bash) walks out through
        // the broken piece: any animal does (zoo.exe 0x414a73: life 0 is open)
        {
          int kt = -1;
          for (size_t k = 0; k < an.types().size(); k++)
            if (an.types()[k].file.find("komodo") != std::string::npos)
              kt = static_cast<int>(k);
          bool walked = false;
          if (kt >= 0 && low.x >= 0) {
            if (bear >= 0) {
              an.remove(bear);
              bear = -1;
            }
            if (!fnc.broken(low))
              fnc.breakPiece(low);
            int kid = -1;
            for (auto [tx, ty] : ex->tiles)
              if (kid < 0)
                kid = an.adopt(kt, false, tx + 0.5f, ty + 0.5f, world->getMap(), fnc);
            for (int i = 0; i < 3000 && kid >= 0; i++) {
              world->update(SDL_GetKeyboardState(nullptr), 0.1f);
              const Animals::Member *k = an.member(kid);
              if (!k)
                break;
              if (k->escaped) {
                walked = true;
                world->centreOn(k->x, k->y);
                capture("komodo_out_of_hole");
                break;
              }
            }
            if (kid >= 0)
              an.remove(kid);
          }
          check("a komodo dragon walks out through a broken fence", walked);
        }
        // A gazelle (a jumper) over the low fence: it jumps it (jump_high,
        // once across) and lands clear on the far side - it walked through
        {
          int gt = an.typeOfFile("animals/gazelle.ai");
          bool jumped = false, clear = false;
          if (gt >= 0 && low.x >= 0) {
            fnc.repair(low);
            int gid = -1;
            for (auto [tx, ty] : ex->tiles)
              if (gid < 0)
                gid = an.adopt(gt, false, tx + 0.5f, ty + 0.5f, world->getMap(), fnc);
            bool shot = false;
            for (int i = 0; i < 3000 && gid >= 0; i++) {
              world->update(SDL_GetKeyboardState(nullptr), 0.05f);
              const Animals::Member *g = an.member(gid);
              if (!g)
                break;
              if (g->anim == "jump_high") {
                jumped = true;
                // (over the fence line: a shot)
                float off = low.alongX ? g->y - low.y : g->x - low.x;
                if (!shot && std::fabs(off) < 0.15f) {
                  shot = true;
                  world->centreOn(g->x, g->y);
                  capture("gazelle_jumps_fence");
                }
              }
              if (g->escaped && g->anim != "jump_high") {
                float off = low.alongX ? g->y - low.y : g->x - low.x;
                clear = std::fabs(off) > 0.3f;
                break;
              }
            }
            if (gid >= 0)
              an.remove(gid);
          }
          check(("a gazelle jumps the low fence: jumped " + std::to_string(jumped) + ", landed clear " +
                 std::to_string(clear)).c_str(), jumped && clear);
        }
        // A fence laid over one replaces it (the original's way to change a
        // fence, zoo.exe 0x486965): new and full, at the new kind's price;
        // the same kind not yet worn is free (worn, full price); a gate stays
        // a gate
        if (low.x >= 0) {
          int chain = fnc.typeIndex("chainlnk"), small = fnc.typeIndex("smchain");
          fnc.previewType = chain;
          bool can = fnc.canPlace(low, world->getMap());
          bool priced = fnc.layCost(low, chain) == fnc.types()[chain].cost;
          fnc.place(low, chain, 0, world->getMap());
          const Fences::Piece *pc = fnc.at(low);
          bool replaced = pc && pc->type == chain && pc->life == static_cast<float>(fnc.types()[chain].life);
          bool sameFree = fnc.canPlace(low, world->getMap()) && fnc.layCost(low, chain) == 0;
          fnc.setLife(low, static_cast<float>(fnc.types()[chain].decayedLife));
          bool wornFull = fnc.layCost(low, chain) == fnc.types()[chain].cost;
          bool gateKept = true;
          for (const auto &[e, gp] : fnc.pieces())
            if (gp.gate && gp.tank < 0) {
              Fences::Edge ge = e;
              int was = gp.type, to = gp.type == chain ? small : chain;
              fnc.previewType = to;
              fnc.place(ge, to, 0, world->getMap());
              const Fences::Piece *g2 = fnc.at(ge);
              gateKept = g2 && g2->gate && g2->type == to;
              fnc.previewType = was;
              fnc.place(ge, was, 0, world->getMap());
              break;
            }
          fnc.previewType = small;
          fnc.place(low, small, 0, world->getMap());
          fnc.previewType = -1;
          check(("a fence laid over one replaces it: can " + std::to_string(can) + ", priced " + std::to_string(priced) +
                 ", replaced " + std::to_string(replaced) + ", same kind free " + std::to_string(sameFree) +
                 ", worn full price " + std::to_string(wornFull) + ", gate kept " + std::to_string(gateKept)).c_str(),
                can && priced && replaced && sameFree && wornFull && gateKept);
        }
        // Every animal's adopting likeness, both sexes (a female baboon's was
        // blank: no stand of its own or the male's, and the fallback missed)
        {
          int missing = 0;
          for (size_t k = 0; k < an.types().size(); k++)
            for (int f = 0; f < 2; f++)
              if (!an.preview(static_cast<int>(k), f == 1) && an.types()[k].file != "animals/mmaid.ai")
                missing++;
          check(("every animal's likeness to place, male and female (" + std::to_string(missing) + " missing)").c_str(),
                missing == 0);
        }
        // A lion with a zebra: the zebra runs, the lion chases it down, a
        // dust ball, and the zebra's gone (zoo.exe 0x43954c, 0x4a6043)
        {
          int lionType = an.typeOfFile("animals/lion.ai");
          bool fled = false, hunted = false, ball = false, gone = false;
          // (the pen's zebra, if the bear left it, kept out of it)
          Animals::Member *pet = an.member(zid);
          int zebraType = an.typeOfFile("animals/zebra.ai");
          if (lionType >= 0 && zebraType >= 0) {
            if (low.x >= 0)
              fnc.setPieceType(low, oldType); // (the pen's own fence: a lion jumps the low one)
            if (pet)
              pet->claimedBy = 999;
            int prey = -1, lion = -1;
            for (auto [tx, ty] : ex->tiles)
              if (prey < 0)
                prey = an.adopt(zebraType, true, tx + 0.5f, ty + 0.5f, world->getMap(), fnc);
            // (the lion as far from it as the pen allows)
            float far = -1, lx = 0, ly = 0;
            if (const Animals::Member *z0 = an.member(prey))
              for (auto [tx, ty] : ex->tiles)
                if (an.canPlace(lionType, tx + 0.5f, ty + 0.5f, world->getMap(), fnc) == Fences::Fit::Ok &&
                    std::hypot(tx + 0.5f - z0->x, ty + 0.5f - z0->y) > far) {
                  far = std::hypot(tx + 0.5f - z0->x, ty + 0.5f - z0->y);
                  lx = tx + 0.5f;
                  ly = ty + 0.5f;
                }
            if (far >= 0)
              lion = an.adopt(lionType, true, lx, ly, world->getMap(), fnc); // (a lioness: the hunter)
            bool shot = false;
            for (int i = 0; i < 4000 && lion >= 0 && prey >= 0; i++) {
              world->update(SDL_GetKeyboardState(nullptr), 0.1f);
              const Animals::Member *l = an.member(lion), *z = an.member(prey);
              if (!l)
                break;
              if (!z) {
                gone = true;
                break;
              }
              fled = fled || z->fleeFrom == lion;
              hunted = hunted || l->huntAnimal == prey;
              if (l->victim == prey && l->anim == "dustball") {
                ball = true;
                an.hovered = lion; // (hovered: the cloud stays unlit - collect checks it)
                if (!shot) {
                  shot = true;
                  world->centreOn(l->x, l->y);
                  capture("lion_catches_zebra");
                }
              }
            }
            if (lion >= 0)
              an.remove(lion);
            if (prey >= 0 && an.member(prey))
              an.remove(prey);
            if (Animals::Member *pz = an.member(zid))
              pz->claimedBy = -1;
            if (low.x >= 0)
              fnc.setPieceType(low, fnc.typeIndex("smchain"));
          }
          check(("a lion hunts a zebra: ran " + std::to_string(fled) + ", chased " + std::to_string(hunted) + ", dust ball " +
                 std::to_string(ball) + ", gone " + std::to_string(gone)).c_str(),
                fled && hunted && ball && gone);
        }
        for (Staff::Member &k : const_cast<std::vector<Staff::Member> &>(world->getStaff().members()))
          k.workCheck = 0;
        if (bear >= 0)
          an.remove(bear);
        if (low.x >= 0) {
          fnc.setPieceType(low, oldType);
          fnc.repair(low);
        }
        // (the zebra back in its pen if it slipped out the gap)
        if (Animals::Member *zb = an.member(zid)) {
          for (auto [tx, ty] : ex->tiles)
            if (an.canPlace(zb->type, tx + 0.5f, ty + 0.5f, world->getMap(), fnc) == Fences::Fit::Ok) {
              zb->x = tx + 0.5f;
              zb->y = ty + 0.5f;
              break;
            }
          zb->escaped = false;
          zb->claimedBy = -1;
          zb->exhibit = ex->id;
          zb->path.clear();
          zb->pathAt = 0;
          zb->stack.clear();
          zb->dying = false;
        }
        world->getAnimals().takeMessages();
        for (const Guests::Guest &g : gs.guests())
          const_cast<Guests::Guest &>(g).state = Guests::State::Walking;
      }
      // Old age
      if (Animals::Member *zz = an.member(zid)) {
        int savedDeath = type.deathChance;
        type.deathChance = 100;
        zz->life = 0;
        zz->reproductionCheck = 0;
        bool sawDie = false;
        for (int i = 0; i < 1200 && an.member(zid); i++) {
          world->update(SDL_GetKeyboardState(nullptr), 0.1f);
          if (const Animals::Member *d = an.member(zid))
            sawDie = sawDie || (d->dying && (d->anim == "lie_down" || d->anim == "sleep" || d->anim == "lie_idle"));
        }
        type.deathChance = savedDeath;
        std::vector<Animals::Notice> notes = an.takeNotices();
        bool said = false;
        for (const Animals::Notice &n : notes)
          said = said || n.text.find("died of old age") != std::string::npos;
        check("old age: its death animation, then gone, the message", sawDie && !an.member(zid) && said);
      }
    }
  }

  // Saving and loading: the zoo as it stands saved, the map started afresh,
  // the save loaded: the same animals, fences, exhibits, objects, staff,
  // guests, money and ground
  {
    std::string file = outDir + "/test.zt1save";
    auto snapshot = [&]() {
      std::string s;
      s += "cash " + std::to_string(std::lround(g_sim.cash()));
      s += " animals " + std::to_string(world->getAnimals().members().size());
      s += " fences " + std::to_string(world->getFences().pieces().size());
      int named = 0;
      for (const Fences::Exhibit &e : world->getFences().exhibits())
        named += e.named ? 1 : 0;
      s += " exhibits " + std::to_string(named);
      s += " objects " + std::to_string(world->getObjects().objects().size());
      s += " staff " + std::to_string(world->getStaff().members().size());
      s += " guests " + std::to_string(world->getGuests().guests().size());
      long h = 0;
      for (int y = 0; y < world->getMap().getHeight(); y++)
        for (int x = 0; x < world->getMap().getWidth(); x++) {
          const MapTile *t = world->getMap().getTile(x, y);
          h += t->cornerHeight[0] * 3 + t->terrainType + (world->getMap().isPath(x, y) ? 7 : 0);
        }
      s += " ground " + std::to_string(h);
      // (tanks: their walls, water and shared walls; animals where they are)
      for (const Fences::Exhibit &e : world->getFences().exhibits())
        if (e.tank)
          s += " tank" + std::to_string(e.id) + ":" + std::to_string(e.wallSub) + "/" + std::to_string(e.floorHeight) +
               (e.salt ? "s" : "f") + std::to_string(static_cast<int>(e.water * 100));
      int shared = 0;
      for (const auto &[pe, pp] : world->getFences().pieces())
        shared += pp.tank2 >= 0 ? 1 : 0;
      s += " shared " + std::to_string(shared);
      for (const Animals::Member &m : world->getAnimals().members()) {
        char b[64];
        std::snprintf(b, sizeof b, " a%d@%.1f,%.1f%s", m.type, m.x, m.y, m.female ? "f" : "m");
        s += b;
      }
      return s;
    };
    std::string before = snapshot();
    bool saved = SaveGame::save(file, *world, g_sim, *g_hud, chosen->path);
    check(("saved the zoo (" + before + ")").c_str(), saved && SaveGame::mapOf(file) == chosen->path);
    bool loaded = world->loadFreeform(SaveGame::mapOf(file)) && SaveGame::load(file, *world, g_sim, *g_hud);
    for (int i = 0; i < 3; i++)
      world->update(SDL_GetKeyboardState(nullptr), 0.05f);
    std::string after = snapshot();
    SDL_Log("[Save] before %s", before.c_str());
    SDL_Log("[Save] after  %s", after.c_str());
    capture("loaded_save");
    check("loaded it back: all the same", loaded && before == after);
    // As a player does: the game menu's Save Game and Load Game (the file
    // dialog's answer given), the zoo back as it was
    {
      // (an animal and a raised tank in it first)
      Animals &an = world->getAnimals();
      int zt = an.typeOfFile("animals/zebra.ai");
      for (const Fences::Exhibit &e : world->getFences().exhibits())
        if (!e.tank && e.named && zt >= 0 && an.members().empty())
          for (auto [tx, ty] : e.tiles)
            if (an.members().empty())
              an.adopt(zt, true, tx + 0.5f, ty + 0.5f, world->getMap(), world->getFences());
      for (const Fences::Exhibit &e : world->getFences().exhibits())
        if (e.tank)
          world->getFences().adjustWall(e.id, 3);
      std::string menuFile = outDir + "/menu_save.zt1save";
      std::filesystem::remove(menuFile);
      g_hud->showPanel(0, "");
      g_hud->toggleGameMenu();
      capture("menu_save_open");
      bool saveButton = click(1502) == UiAction::GAME_SAVE;
      std::string was = snapshot();
      bool wrote = gameSave(menuFile) && std::filesystem::exists(menuFile);
      // (things change after saving: all of it undone by loading)
      for (const Fences::Exhibit &e : world->getFences().exhibits())
        if (e.tank)
          world->getFences().adjustWall(e.id, -2);
      if (!an.members().empty())
        an.remove(an.members().front().id);
      if (!g_hud->isPanelOpen(5))
        g_hud->toggleGameMenu();
      capture("menu_load_open");
      bool loadButton = click(1501) == UiAction::GAME_LOAD;
      bool read = gameLoad(rm, menuFile);
      for (int i = 0; i < 3; i++)
        world->update(SDL_GetKeyboardState(nullptr), 0.05f);
      std::string now = snapshot();
      SDL_Log("[Save] menu before %s", was.c_str());
      SDL_Log("[Save] menu after  %s", now.c_str());
      capture("menu_loaded");
      check(("the game menu saves and loads the zoo: buttons " + std::to_string(saveButton) + std::to_string(loadButton) +
             ", written " + std::to_string(wrote) + ", read " + std::to_string(read) + ", same " +
             std::to_string(was == now))
                .c_str(),
            saveButton && loadButton && wrote && read && was == now);
      world->getAnimals().takeMessages();
    }
  }

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
  // Sound effects, and every button's click (uiclick.wav)
  Sound::get().init(&resource_manager);
  UiButton::clickSound = [] { Sound::get().play("sounds/uiclick"); };
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
  // The original's cursors (res0.dll): the arrow, hot spot at its tip, and
  // the full bulldozer while it's picked
  g_arrowCursor = resource_manager.getCursor(9, 1, 2);
  g_bulldozeCursor = resource_manager.getCursor(16, 2, 29);
  window.set_cursor(g_arrowCursor);

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
  // ZT_CHECK_PREVIEWS=1: log each animal whose adopting likeness (either
  // sex) has no art, and quit
  if (std::getenv("ZT_CHECK_PREVIEWS")) {
    Animals &an = g_world->getAnimals();
    an.loadTypes(&resource_manager);
    for (size_t k = 0; k < an.types().size(); k++)
      for (int f = 0; f < 2; f++)
        if (!an.preview(static_cast<int>(k), f == 1))
          SDL_Log("[PREVIEW] none: %s %s", an.types()[k].file.c_str(), f ? "female" : "male");
    SDL_Log("[PREVIEW] checked %d", static_cast<int>(an.types().size()));
    return 0;
  }

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

  LayoutState lastState = g_currentState;
  while (running > 0) {
    // The menus have music, a game has none (the world's ambience instead)
    if (g_currentState != lastState) {
      if (g_currentState == LayoutState::GAME_LOOP)
        resource_manager.fadeMenuMusic();
      else if (lastState == LayoutState::GAME_LOOP) {
        Mix_HaltChannel(-1);
        resource_manager.playMenuMusic();
      }
      lastState = g_currentState;
    }
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
      // ESC in a game, as in the original: laying a walkway, it stops
      // that; else it closes what's on top, one at a time (the window
      // opened last first); with nothing open, it opens the game menu
      // (the bulldozer out: Esc puts it away first - an addition, "hotkeys")
      bool escUsed = false;
      if (input.event == InputEvent::KEY_ESCAPE && !UiText::isEditing() && Features::hotkeys &&
          g_currentState == LayoutState::GAME_LOOP && g_hud && g_hud->bulldozerOn()) {
        g_hud->bulldozerOff();
        escUsed = true;
      }
      if (input.event == InputEvent::KEY_ESCAPE && !escUsed && !UiText::isEditing() &&
          g_currentState == LayoutState::GAME_LOOP && g_hud && !(g_world && g_world->cancelLine()) &&
          !g_hud->closeTopmost())
        g_hud->toggleGameMenu();
      // Ctrl+Z: Undo, as its button ("hotkeys")
      if (input.event == InputEvent::KEY_DOWN && input.key == SDLK_z && (SDL_GetModState() & KMOD_CTRL) &&
          Features::hotkeys && !UiText::isEditing() && !g_console.isOpen() &&
          g_currentState == LayoutState::GAME_LOOP && g_hud && g_world && g_world->canUndo())
        if (UiButton *undo = dynamic_cast<UiButton *>(g_hud->getElementById(1075)))
          if (undo->onClick)
            undo->onClick();
      // PageUp / PageDown: the height paths are laid at (elevated paths)
      if (input.event == InputEvent::KEY_DOWN &&
          (input.key == SDLK_PAGEUP || input.key == SDLK_PAGEDOWN) && !UiText::isEditing() &&
          g_currentState == LayoutState::GAME_LOOP && g_world && !g_world->getPathTool().empty() &&
          Features::elevatedPaths)
        g_world->adjustBuildHeight(input.key == SDLK_PAGEUP ? 1 : -1);
      // Q / E turn the view left / right, as the rotate buttons - or, with
      // an object to put down, turn the object ("hotkeys")
      if (input.event == InputEvent::KEY_DOWN && (input.key == SDLK_q || input.key == SDLK_e) &&
          !UiText::isEditing() && !g_console.isOpen() && g_currentState == LayoutState::GAME_LOOP &&
          g_world) {
        if (Features::hotkeys && g_hud && !g_world->getObjectTool().empty())
          g_hud->rotateHeld(input.key == SDLK_e ? 1 : -1);
        else
          g_world->rotateView(input.key == SDLK_e ? 1 : -1);
      }
      // Tab (Shift+Tab back): how a path or walkway drag lays out
      if (input.event == InputEvent::KEY_DOWN && input.key == SDLK_TAB && !UiText::isEditing() &&
          g_currentState == LayoutState::GAME_LOOP && g_world && !g_world->getPathTool().empty() &&
          Features::pathModes)
        g_world->cyclePathMode((SDL_GetModState() & KMOD_SHIFT) ? -1 : 1);
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
      action = gameInputs(window.renderer, &resource_manager, inputs);
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

    // The main menu's Load Saved Game (the dialog) and Continue Saved Game
    // (the latest save)
    case UiAction::STARTUP_LOAD_GAME:
    case UiAction::STARTUP_CONTINUE_GAME: {
      std::string path;
      if (action == UiAction::STARTUP_LOAD_GAME) {
        path = SaveGame::askLoadPath();
      } else {
        std::filesystem::file_time_type newest{};
        std::error_code ec;
        for (const auto &e : std::filesystem::directory_iterator(SaveGame::folder(), ec))
          if (e.path().extension() == ".zt1save" && (path.empty() || e.last_write_time() > newest)) {
            newest = e.last_write_time();
            path = e.path().string();
          }
      }
      if (!path.empty() && gameLoad(&resource_manager, path)) {
        delete layout;
        layout = nullptr;
        g_currentState = LayoutState::GAME_LOOP;
      }
      break;
    }
    // Save Game: the Windows "Save a zoo..." dialog, as the original
    case UiAction::GAME_SAVE:
      gameSave();
      break;
    // Load Game: "Load a zoo...", its map started afresh, then the save
    case UiAction::GAME_LOAD:
      gameLoad(&resource_manager);
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
          // (a save names it: "scenario:" and its .scn)
          g_currentMapPath = "scenario:" + info->scenarioPath;
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
          g_currentMapPath = map->path;
          createHud(&resource_manager, g_world, g_currentStartingCash);
          startGoals(&resource_manager, map->path);

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
      if (g_hud)
        setGamePaused(action == UiAction::HUD_PAUSE);
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
      // A question or a name to give (a dialog up): the zoo waits, as the
      // original's, and the camera keys are the box's
      bool dialogUp = g_hud && g_hud->dialogOpen();
      if (dialogUp && !g_world->isPaused()) {
        setGamePaused(true);
        g_dialogPaused = true;
      } else if (!dialogUp && g_dialogPaused) {
        g_dialogPaused = false;
        if (!g_popupPaused)
          setGamePaused(false);
      }
      g_world->update(g_console.isOpen() || UiText::isEditing() || dialogUp ? noKeys : state, deltaTime * g_gameSpeed);
      // The clock runs while the game isn't paused
      if (!g_world->isPaused()) {
        g_sim.update(deltaTime * g_gameSpeed);
        g_goals.update(deltaTime * g_gameSpeed);
        // Research: programs finished, their effects, and the news
        // ("Research & Conservation: %r is now available.", 23020; "%s: All
        // selected programs have been completed.", 23021)
        Research::Tick rt = Research::get().advance(deltaTime * g_gameSpeed, g_sim.cash());
        for (ResearchProgram *p : rt.completed) {
          applyResearch(*p);
          std::string m = resource_manager.getString(23020);
          size_t at = m.find("%r");
          if (at != std::string::npos)
            m.replace(at, 2, p->name);
          g_hud->postMessage(m, 1);
        }
        for (const std::string &b : rt.finishedBranches) {
          std::string m = resource_manager.getString(23021);
          size_t at = m.find("%s");
          if (at != std::string::npos)
            m.replace(at, 2, b);
          g_hud->postMessage(m);
        }
      }
      resumeAfterPopup();
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
            // Members: each may lapse (rand 0-100 less 10 above the zoo
            // rating); the rest give $100-$300 each (Private Donations)
            {
              Guests &gs = g_world->getGuests();
              int stay = 0;
              for (int m = 0; m < gs.members; m++)
                stay += std::rand() % 101 - 10 > static_cast<int>(g_sim.rating()) ? 0 : 1;
              gs.members = stay;
              if (stay > 0)
                g_sim.earn(ZooSim::Donations, stay * (100 + (std::rand() % 101) * 200 * 0.01));
            }
            // And every building's upkeep (its cUpkeep, Zoo Upkeep Cost)
            for (PlacedObjects::Object &o : g_world->objectsMutable().all()) {
              if (o.label.empty())
                continue;
              // (this month's visitors become last month's)
              o.visitorsLast = o.visitorsNow;
              o.visitorsNow = 0;
              static std::map<std::string, float> upkeepOf;
              std::string file = PlacedObjects::fileOf(o);
              auto u = upkeepOf.find(file);
              if (u == upkeepOf.end()) {
                float v = 0;
                if (IniReader *ai = resource_manager.getIniReader(file)) {
                  std::string s = ai->get("characteristics/floats", "cupkeep");
                  v = s.empty() ? 0.0f : static_cast<float>(std::atof(s.c_str()));
                  delete ai;
                }
                u = upkeepOf.emplace(file, v).first;
              }
              float monthly = u->second;
              // (an animal house: its program's upkeep instead of cUpkeep)
              if (o.nameId) {
                std::vector<const ResearchProgram *> shows = Research::get().collectionFor(o.nameId);
                if (!shows.empty())
                  monthly = static_cast<float>(shows[std::clamp(o.program, 0, static_cast<int>(shows.size()) - 1)]->effectVal[0]);
              }
              if (monthly > 0) {
                g_sim.spend(ZooSim::Upkeep, monthly);
                o.upkeep += monthly;
              }
            }
          }
          g_world->getFences().setMonth(months);
          if (g_hud)
            g_hud->monthNumber = months;
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
      if (g_snapshot == 1) { // (the map alone)
        g_snapshot = 0;
        takeSnapshot(window.renderer, &resource_manager);
      }
      updateZoomButtons(g_world); // the mouse wheel zooms too
      drawHud(window.renderer, &resource_manager);
      drawToolCost(window.renderer, &resource_manager);
      if (g_snapshot == 2) { // (the screen as it is)
        g_snapshot = 0;
        takeSnapshot(window.renderer, &resource_manager);
      }
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