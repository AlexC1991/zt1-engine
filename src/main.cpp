#define SDL_MAIN_HANDLED
#include <ctime>
#include <SDL3/SDL_main.h>

#include <SDL3/SDL.h>
#include <algorithm>
#include <climits>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <vector>
#include <string>
#include <thread>
#include <unordered_map>
#include <deque>
#include <set>

#include "Animation.hpp"
#include "ArtScaler.hpp"
#include "GpuCompressed.hpp"
#include "GpuFsr.hpp"
#include "GraphicsEffects.hpp"
#include "ItemCatalog.hpp"
#include "Embed.hpp"
#include "Lab.hpp"
#include "CompassDirection.hpp"
#include "Config.hpp"
#include "RenderSettings.hpp"
#include "IniReader.hpp"
#include "Input.hpp"
#include "InputManager.hpp"
#include "LoadScreen.hpp"
#include "RenameCheats.hpp"
#include "Celebrations.hpp"
#include "Research.hpp"
#include "ResourceManager.hpp"
#include "ScenarioManager.hpp"

#include "Utils.hpp"
#include "Window.hpp"
#include "World.hpp"
#include "SpriteDatabase.hpp"
#include "SpriteManager.hpp"
#include "DevConsole.hpp"
#include "Features.hpp"
#include "GameSpeedClock.hpp"
#include "GameBenchmark.hpp"
#include "GraphicsQuality.hpp"
#include "Audio.hpp"
#include "SampleProfiler.hpp"
#include "ModMenu.hpp"
#include "DisplaySettings.hpp"
#include "HdArt.hpp"
#include "ModImport.hpp"
#include "ModRepo.hpp"
#include "Mods.hpp"
#include "ShaderMods.hpp"
#include "Pathfinder.hpp"
#include <random>
#include <sstream>
#include <iomanip>
#include <queue>
#include "ZooSim.hpp"
#include "ZooReader.hpp"
#include <fstream>
#include <map>
#include "OriginalConstants.hpp"
#include "Paths.hpp"
#include "Updater.hpp"
#include "Settings.hpp"
#include "Sound.hpp"
#include "SaveGame.hpp"
#include "Goals.hpp"
#include "LangText.hpp"
#include <SDL3_image/SDL_image.h>

#include "ui/UiImage.hpp"
#include "ui/UiLayout.hpp"
#include "ui/UiListBox.hpp"
#include "ui/UiScrollingRegion.hpp"
#include "ui/UiButton.hpp"
#include "ui/UiOptions.hpp"
#include "ui/UiUpdate.hpp"
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
  MOD_MENU, // Get New Zoo Tycoon Items, with the "modmenu" switch on (ModMenu.hpp)
  GAME_LOOP
};
LayoutState g_currentState = LayoutState::MAIN_MENU;
static int g_esthetic = 0; // the zoo rating's esthetics term (0-10)
// A scenario's ending (zoo.exe scenarioComplete 0x48a408): its outcome as the
// goals report it, and what its dialog chose - done between frames
static int g_scenarioOutcome = 0;
// A zoo just loaded or started: rated on the next frame, paused or not
// (zoo.exe worldAfterLoad 0x4534e6 -> zooRatingTerms 0x41fcc1 at once)
static bool g_rateNow = false;
static void setRenameHooks(ResourceManager *rm);
static std::vector<int> g_queuedActions; // (UiActions)
static std::string g_nextScenario;

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
// Starting cash spinner, as the original's (zoo.exe mapselec_Panel_show
// 0x4cb4b1): zoo.ini [UI] MSStartingCash (50000 if not there) kept between
// MSMinCash and MSMaxCash, stepped by MSCashIncrement (5000), and each
// change written back to zoo.ini (0x4cb384), so the last choice comes back
const int CASH_START = 75000; // (a scenario's own start, not the spinner's)
int CASH_STEP = 5000;
int CASH_MIN = 10000;
int CASH_MAX = 500000;
int g_currentStartingCash = CASH_START;
static ResourceManager *g_cashStrings = nullptr;

static void showStartingCash() {
  if (!g_startingCashText)
    return;
  // "$75,000"
  std::string digits = std::to_string(g_currentStartingCash);
  for (int i = static_cast<int>(digits.size()) - 3; i > 0; i -= 3)
    digits.insert(i, ",");
  g_startingCashText->setText("$" + digits);
  // Its difficulty: under $25,000 hard, over $100,000 easy, else
  // intermediate (0x4cb384: strings 11538 / 11536 / 11537)
  if (g_difficultyText && g_cashStrings) {
    int id = g_currentStartingCash < 25000 ? 11538 : g_currentStartingCash > 100000 ? 11536 : 11537;
    g_difficultyText->setText(g_cashStrings->getString(id));
  }
}

static void loadStartingCash() {
  CASH_MIN = Settings::getInt("UI", "MSMinCash", 10000);
  CASH_MAX = std::max(CASH_MIN, Settings::getInt("UI", "MSMaxCash", 500000));
  CASH_STEP = std::max(1, Settings::getInt("UI", "MSCashIncrement", 5000));
  g_currentStartingCash = std::clamp(Settings::getInt("UI", "MSStartingCash", 50000), CASH_MIN, CASH_MAX);
}

static void setStartingCash(int cash) {
  g_currentStartingCash = std::clamp(cash, CASH_MIN, CASH_MAX);
  Settings::setInt("UI", "MSStartingCash", g_currentStartingCash);
  showStartingCash();
}

// The starting cash's arrows (zoo.exe mapselec_UpSpinner_click 0x616350,
// mapselec_DownSpinner_click 0x6163b2): the other arrow is enabled again
// (uiFind 0x2cf8 / 0x2cf7, slot 0x68); the cash goes up or down by
// MSCashIncrement (0x641b3c) and at MSMaxCash or MSMinCash (0x6433d0,
// 0x6433c8) is held there and this arrow greyed (slot 0x6c); written back
// and shown (0x4cb384). dir 1 up, -1 down
static void stepStartingCash(UiLayout *layout, int dir) {
  UiElement *self = layout ? layout->getElementById(dir > 0 ? 11511 : 11512) : nullptr;
  UiElement *other = layout ? layout->getElementById(dir > 0 ? 11512 : 11511) : nullptr;
  if (other)
    other->setDisabled(false);
  setStartingCash(g_currentStartingCash + dir * CASH_STEP);
  if (self && (dir > 0 ? g_currentStartingCash >= CASH_MAX : g_currentStartingCash <= CASH_MIN))
    self->setDisabled(true);
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

    // The picture the scenario itself names ([desc] lockedpicture, a folder
    // and file such as ui/scenario/lockj/lockj: zoo.exe 0x6000ac); the guess
    // above only when it names none
    if (IniReader *ini = rm->getIniReader(scnPath)) {
      std::string pic = ini->get("desc", "lockedpicture");
      delete ini;
      while (!pic.empty() && std::isspace(static_cast<unsigned char>(pic.back())))
        pic.pop_back();
      while (!pic.empty() && std::isspace(static_cast<unsigned char>(pic.front())))
        pic.erase(pic.begin());
      const size_t slash = pic.find_last_of('/');
      if (slash != std::string::npos && pic.find('.') == std::string::npos) {
        lockFolder = pic.substr(0, slash);
        lockFileName = pic.substr(slash + 1);
      }
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
// The original's menu layouts (*.lyt) are authored for 800x600. The
// original draws everything 1:1 whatever its screen size: menus centred,
// the in-game screen filling it with its bars and panels pinned to the
// edges. So do we. Game Options > Interface Size (like OpenRCT2's UI scale;
// zoo.ini [user] uiScale, RenderSettings::uiScaleSetting) makes it bigger:
// Auto takes whole steps up on a big window - 1600x1200 and up draws it all
// 2x, 3x..., so the art stays sharp - or a scale picked, kept small enough
// for the 800x600 screens to fit. A window under 800x600 is shrunk to fit.
// Text is rendered by the font at the final size so it stays sharp; art is
// filtered smoothly unless the scale is a whole number. Mouse positions are
// mapped back into layout space.
// ============================================================================

static bool g_widescreen = true;      // in-game screen fills the window
// Freeform items month by month (the original), or all from the start
static bool g_timedUnlocks = false;
// The month the catalogue lists items for: every freeform unlock goal's
// month has passed when they're all unlocked
// (ours: a scenario has no freeform unlock goals - what it can buy is what
// its own triggers leave, so the month rule passes everything there)
static std::string g_currentMapPath; // the map the game was started on (saves name it)
static int unlockMonthFor(int month) {
  return Features::allUnlocked || g_currentMapPath.rfind("scenario:", 0) == 0 ? 1000 : month;
}
static const int UI_LAYOUT_W = 800;
static const int UI_LAYOUT_H = 600;

struct UiTransform {
  float scale = 1.0f;
  float offsetX = 0.0f;
  float offsetY = 0.0f;
};

static UiTransform getUiTransform(SDL_Renderer *renderer) {
  int w = UI_LAYOUT_W, h = UI_LAYOUT_H;
  SDL_GetCurrentRenderOutputSize(renderer, &w, &h);
  UiTransform t;
  t.scale = RenderSettings::uiScaleFor(w, h);
  // The viewport is set in layout units, so snap the offset to them; mouse
  // mapping uses the same snapped offset
  t.offsetX = std::round((w - UI_LAYOUT_W * t.scale) * 0.5f / t.scale) * t.scale;
  t.offsetY = std::round((h - UI_LAYOUT_H * t.scale) * 0.5f / t.scale) * t.scale;
  return t;
}

// The in-game screen's transform: the UI's scale, from the window's corner
// when it fills the window (widescreen, the default), else the centred box's
static UiTransform hudTransform(SDL_Renderer *renderer) {
  UiTransform t = getUiTransform(renderer);
  if (g_widescreen)
    t.offsetX = t.offsetY = 0;
  return t;
}

// The Options window's transform: from the window's corner (it centres
// itself), at the UI's scale - or, while its Interface Size slider is
// dragged, the size it had when the slider was taken, the rest of the UI
// growing and shrinking around it (RenderSettings::uiScaleHeld)
static UiTransform optionsTransform(SDL_Renderer *renderer) {
  int w = UI_LAYOUT_W, h = UI_LAYOUT_H;
  SDL_GetCurrentRenderOutputSize(renderer, &w, &h);
  UiTransform t;
  t.scale = RenderSettings::uiScaleHeld > 0.0f ? RenderSettings::uiScaleHeld : RenderSettings::uiScaleFor(w, h);
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
  SDL_GetCurrentRenderOutputSize(renderer, &outW, &outH);

  if (GpuFsr::upscales(t.scale)) {
    GpuFsr::beginLayer(renderer, GpuFsr::Layer::Ui,
                       static_cast<int>(std::ceil(outW / t.scale)),
                       static_cast<int>(std::ceil(outH / t.scale)),
                       SDL_Color{0, 0, 0, 0});
    RenderSettings::artScaleMode = SDL_SCALEMODE_NEAREST;
    RenderSettings::uiPass = RenderSettings::UiPass::Art;
    draw(renderer);
    GpuFsr::endLayer(renderer, GpuFsr::Layer::Ui, outW / t.scale,
                     outH / t.scale, true);
    RenderSettings::uiPass = RenderSettings::UiPass::Text;
  } else if (RenderSettings::artLook == RenderSettings::ArtLook::Smooth && std::fabs(t.scale - 1.0f) > 0.001f) {
    // Smooth (Art upscaling): the art drawn 1:1 into a clear layer and the
    // whole layer smoothed up to the window at once, as FSR's - each piece
    // smoothed on its own left lines where pieces meet - then the text sharp
    // at the window's size over it
    static SDL_Texture *layer = nullptr;
    static int lw = 0, lh = 0;
    int w = static_cast<int>(std::ceil(outW / t.scale)), h = static_cast<int>(std::ceil(outH / t.scale));
    if (!layer || lw != w || lh != h) {
      if (layer)
        SDL_DestroyTexture(layer);
      layer = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_TARGET, w, h);
      lw = w;
      lh = h;
      if (layer) {
        RenderSettings::setScaleMode(layer, SDL_SCALEMODE_LINEAR);
        // (drawn over clear black, its colours already carry their alpha)
        SDL_SetTextureBlendMode(layer, SDL_ComposeCustomBlendMode(
                                           SDL_BLENDFACTOR_ONE, SDL_BLENDFACTOR_ONE_MINUS_SRC_ALPHA, SDL_BLENDOPERATION_ADD,
                                           SDL_BLENDFACTOR_ONE, SDL_BLENDFACTOR_ONE_MINUS_SRC_ALPHA, SDL_BLENDOPERATION_ADD));
      }
    }
    if (layer) {
      SDL_Texture *was = SDL_GetRenderTarget(renderer);
      SDL_SetRenderTarget(renderer, layer);
      SDL_BlendMode mode;
      SDL_GetRenderDrawBlendMode(renderer, &mode);
      SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
      SDL_SetRenderDrawColor(renderer, 0, 0, 0, 0);
      SDL_RenderClear(renderer);
      SDL_SetRenderDrawBlendMode(renderer, mode);
      RenderSettings::artScaleMode = SDL_SCALEMODE_NEAREST;
      RenderSettings::uiPass = RenderSettings::UiPass::Art;
      draw(renderer);
      SDL_SetRenderTarget(renderer, was);
      SDL_FRect dst = {0.0f, 0.0f, w * t.scale, h * t.scale};
      ZT_RenderCopyF(renderer, layer, nullptr, &dst);
      RenderSettings::uiPass = RenderSettings::UiPass::Text;
    }
  } else {
    RenderSettings::artScaleMode = RenderSettings::lookFor(t.scale);
  }
  SDL_SetRenderScale(renderer, t.scale, t.scale);
  draw(renderer);
  SDL_SetRenderScale(renderer, 1.0f, 1.0f);
  RenderSettings::uiPass = RenderSettings::UiPass::All;
  RenderSettings::artScaleMode = SDL_SCALEMODE_NEAREST;
}

// The Options window (over the main menu or a zoo), centred in the window
// at optionsTransform's size
static void drawOptions(SDL_Renderer *renderer, ResourceManager *rm, UiOptions *options) {
  if (!options || !options->isOpen())
    return;
  UiTransform t = optionsTransform(renderer);
  int w = UI_LAYOUT_W, h = UI_LAYOUT_H;
  SDL_GetCurrentRenderOutputSize(renderer, &w, &h);
  SDL_Rect screen = {0, 0, static_cast<int>(w / t.scale), static_cast<int>(h / t.scale)};
  drawUiScaled(renderer, rm, t, [&](SDL_Renderer *r) { options->draw(r, &screen); });
}

static void drawLayoutCentered(SDL_Renderer *renderer, UiLayout *layout,
                               ResourceManager *rm) {
  UiTransform t = getUiTransform(renderer);
  drawUiScaled(renderer, rm, t, [&](SDL_Renderer *r) {
    // Viewport is given in layout units
    SDL_Rect viewport = {static_cast<int>(std::lround(t.offsetX / t.scale)),
                         static_cast<int>(std::lround(t.offsetY / t.scale)),
                         UI_LAYOUT_W, UI_LAYOUT_H};
    SDL_SetRenderViewport(r, &viewport);
    SDL_Rect layoutRect = {0, 0, UI_LAYOUT_W, UI_LAYOUT_H};
    layout->draw(r, &layoutRect);
    SDL_SetRenderViewport(r, nullptr);
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
// The months played as the unlock goals count them (rule 7/2, zoo.exe
// 0x41d665: the game manager's month + year * 12, both from 0 - measured:
// 0 all of the first January)
static int monthsPlayed() { return g_sim.month() + (g_sim.year() - 1) * 12; }
namespace FrameProfile {void start();void begin();void end();void phase(const char *name);}
#ifdef _WIN32
namespace D3D11Util {void installTextureMaker(SDL_Renderer *renderer);} // (D3D11Util.hpp)
#endif
static DevConsole g_console;    // ` or F10: commands to set up test states
static float g_gameSpeed = 1.0f; // addition: HUD speed control and developer override
static GameSpeedClock g_speedClock;
static bool g_resetSimulationFrame=false;
static bool g_speedControlWasEnabled=Features::fastForward;

// addition: disabling the optional control immediately returns the zoo to
// normal speed. Developer speed commands remain available while it is off.
static void syncGameSpeedControl() {
  if (g_speedControlWasEnabled && !Features::fastForward) {
    g_gameSpeed=1;
    g_speedClock.clearPending();
  }
  g_speedControlWasEnabled=Features::fastForward;
  if (g_hud) g_hud->setGameSpeed(static_cast<int>(g_gameSpeed));
}

static std::string formatMoney(int amount) {
  std::string digits = std::to_string(amount < 0 ? -amount : amount);
  for (int i = static_cast<int>(digits.size()) - 3; i > 0; i -= 3)
    digits.insert(i, ",");
  return (amount < 0 ? "-$" : "$") + digits;
}

// A scenario's own economy file over economy.cfg (zoo.exe applyStartSettings
// 0x58ed1f), then the admission held to its limits, as loadConfig 0x591c0f
// ends (setAdmissionPrice 0x4bbf10: Dinosaur Digs' free-admission scenario
// has $0 to $0)
static void applyScenarioEconomy(ResourceManager *rm, const std::string &scn) {
  Original::loadEconomy(rm, scn);
  if (!g_hud) return;
  UiGameScreen::ZooInfo zi = g_hud->zooInfo();
  const float held = Original::clampAdmission(static_cast<float>(zi.admission));
  if (held != static_cast<float>(zi.admission)) {
    zi.admission = held;
    g_hud->setZooInfo(zi);
  }
}

// The HUD's date ("Apr, Year 1": lang string 22012's MMM',' 'Year' y) and
// money (green, red when below zero)
static void updateHudDate(ResourceManager *rm) {
  if (!g_hud) return;
  // addition: use the exact hour prepared for this frame's sky, including
  // developer previews. The original calendar remains untouched.
  std::string time;
  if (Features::gameTimeShown() && g_world)
    time = DayNightLighting::formatTime(Features::dayNightLighting
        ? g_world->getLighting().displayedHour() : DayNightLighting::hourAt(g_world->getClock()));
  // addition (Features::celebrations): the day too, "25 Dec, Year 1" (off:
  // the original's "Dec, Year 1")
  const std::string day = Features::celebrations ? std::to_string(g_sim.dayOfMonth()) + " " : "";
  g_hud->setDateTime(day + rm->getString(22101 + g_sim.month()) + ", Year " + std::to_string(g_sim.year()), time);
}
static void updateHudClock(ResourceManager *rm) {
  if (!g_hud)
    return;
  updateHudDate(rm);
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
// greyed out. The original's three levels (zoo.exe main_ZoomIn_click
// 0x4b081b, main_ZoomOut_click 0x4b0779: -2, 0, 2 - half, normal, double):
// Zoom Out greys at half, Zoom In at double (at normal too only on an
// 800-wide screen with its video flag set, not a size this engine has)
static const float HUD_ZOOM_MAX = 2.0f;
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

// Undo: the last thing done (since the last click on the map) undone, its
// money back (or a bulldozing's refund taken back) - zoo.exe
// main_SingleUndo_click 0x4de9ce -> undoLastAction 0x4de527
static void undoAndBook() {
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
}

static void createHud(ResourceManager *rm, World *world, int startingCash) {
  destroyHud();
  // The HUD and its panels, as the original's in-game screen
  g_hud = g_nextHud ? g_nextHud
                    : new UiGameScreen(rm, "ui/main.lyt", "ui/gamescrn.lyt");
  g_nextHud = nullptr;
  // (the Help window's About topic: Version and the product number)
  g_hud->helpVersion = std::string(Updater::version()) + (Updater::testBuild() ? " (test)" : "");
  g_hud->helpProductId = Paths::productId();

  // addition: each zoo starts at normal speed; changing rate never unpauses.
  g_gameSpeed=1;
  g_speedClock.reset();
  g_hud->setGameSpeed(1);
  g_hud->onGameSpeed=[](int speed) {
    if (!Features::fastForward || (speed!=1 && speed!=2 && speed!=5)) return;
    g_gameSpeed=static_cast<float>(speed);
    g_speedClock.clearPending();
  };

  // Undo starts visible but greyed out (nothing to undo yet); the Scenario
  // button is greyed out in freeform games
  // (Snap Shot, zoo.exe main_SnapShot_click 0x532df9 -> saveScreenshot
  // 0x532c75: Shift held, the screen as it is; else the map alone)
  if (UiButton *snap = dynamic_cast<UiButton *>(g_hud->getElementById(1099)))
    snap->onClick = [] { g_snapshot = (SDL_GetModState() & SDL_KMOD_SHIFT) ? 2 : 1; };
  if (UiButton *undo = dynamic_cast<UiButton *>(g_hud->getElementById(1075))) {
    undo->setHidden(false);
    undo->setDisabled(true);
    // Undo: the last thing done (since the last click on the map) undone,
    // its money back (or a bulldozing's refund taken back) - zoo.exe
    // main_SingleUndo_click 0x4de9ce -> undoLastAction 0x4de527
    undo->onClick = [] { undoAndBook(); };
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
  Original::loadEconomy(rm);
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
  ItemCatalog::get().setMonth(unlockMonthFor(0));
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
  g_hud->staffAssigning = [world] { return world->isAssigning(); };
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
    if (accept && c > 0 && g_sim.canAfford(c)) {
      g_sim.spend(ZooSim::Construction, tt.accept());
      updateHudClock(rm);
    } else if (!accept) {
      tt.undo();
    }
    world->getAnimals().markDirty();
  };
  // Buildings bought: their panel; sold for 80% of their price back, as it
  // is (ZTEntity::objectSellValue 0x41ec7f; removeObject 0x50a0b0 books the
  // float on Recycling Benefit, bookRemovalRefund 0x4f9329)
  g_hud->setObjects(&world->objectsMutable(), [world, rm](int id) {
    PlacedObjects &po = world->objectsMutable();
    int i = po.indexOf(id);
    if (i < 0)
      return;
    std::string file = PlacedObjects::fileOf(po.objects()[i]);
    for (const CatalogItem &item : ItemCatalog::get().all())
      if (item.file == file)
        g_sim.earn(ZooSim::Recycling, item.cost * Original::sellFactor);
    po.remove(i);
    world->reindex();
    world->getAnimals().markDirty();
    updateHudClock(rm);
  });
  g_hud->guestPickUp = [world](int id) { world->pickUpGuest(id); };
  g_hud->guestRenamed = [world, rm](int id, const std::string &name) {
    auto &list = world->getGuests().mutableGuests();
    for (size_t i = 0; i < list.size(); i++)
      if (list[i].id == id) {
        list[i].name = name;
        RenameCheats::guestRenamed(*world, static_cast<int>(i), name);
        updateHudClock(rm);
        break;
      }
  };
  g_hud->staffRenamed = [world, rm](int id, const std::string &name) {
    RenameCheats::staffRenamed(*world, id, name);
    updateHudClock(rm);
  };
  g_hud->exhibitRenamed = [world, rm](const std::string &name) {
    RenameCheats::exhibitRenamed(*world, name);
    updateHudClock(rm);
  };
  g_hud->animalRenamed = [world, rm](int id, const std::string &name) {
    RenameCheats::animalRenamed(*world, id, name);
    updateHudClock(rm);
  };
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
  g_hud->setShows(&world->getShows());
  g_hud->setAnimals(&world->getAnimals(), [world, rm](UiGameScreen::AnimalRequest r, int id) {
    switch (r) {
    case UiGameScreen::AnimalRequest::PickUp: world->pickUpAnimal(id); break;
    case UiGameScreen::AnimalRequest::Sell: {
      // (zoo.exe removeObject 0x50a0b0: an animal's money back booked by
      // bookRefund 0x48d2da, book 10 - the Finances' Recycling Benefit row
      // (financesRefresh's rows 0x635a90: 3939 sums books 9 and 10), not
      // Animal Purchases)
      double refund = world->getAnimals().sellValue(id);
      world->sellAnimal(id);
      if (refund > 0)
        g_sim.earn(ZooSim::Recycling, refund);
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
  SDL_GetCurrentRenderOutputSize(renderer, &w, &h);
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
  SDL_GetCurrentRenderOutputSize(renderer, &w, &h);
  if (g_widescreen)
    return {0, 0, w, h};
  UiTransform t = getUiTransform(renderer);
  return {static_cast<int>(std::lround(t.offsetX)),
          static_cast<int>(std::lround(t.offsetY)),
          static_cast<int>(std::lround(UI_LAYOUT_W * t.scale)),
          static_cast<int>(std::lround(UI_LAYOUT_H * t.scale))};
}

// A dialog over the Options window (it's drawn after the HUD): drawn again
// on top, so a question it asked - Exit Game's, Main Menu's - shows
static void drawDialogOverOptions(SDL_Renderer *renderer, ResourceManager *rm) {
  if (!g_hud || !g_hud->optionsOpen() || !g_hud->hasDialog())
    return;
  UiTransform t = getUiTransform(renderer);
  drawUiScaled(renderer, rm, t, [&](SDL_Renderer *r) {
    if (g_widescreen) {
      SDL_Rect rect = hudLayoutRect(renderer, t);
      g_hud->drawDialog(r, &rect);
      return;
    }
    SDL_Rect viewport = {static_cast<int>(std::lround(t.offsetX / t.scale)),
                         static_cast<int>(std::lround(t.offsetY / t.scale)), UI_LAYOUT_W, UI_LAYOUT_H};
    SDL_SetRenderViewport(r, &viewport);
    SDL_Rect layoutRect = {0, 0, UI_LAYOUT_W, UI_LAYOUT_H};
    g_hud->drawDialog(r, &layoutRect);
    SDL_SetRenderViewport(r, nullptr);
  });
}

static void drawHud(SDL_Renderer *renderer, ResourceManager *rm) {
  if (!g_hud)
    return;
  updateHudDate(rm);
  // (Zoo Status redone every 10 s while open, not while paused: zoo.exe 0x401644)
  g_hud->zooStatusTick(SDL_GetTicks(), g_world && g_world->isPaused());
  UiTransform t = getUiTransform(renderer);
  if (g_widescreen) {
    SDL_Rect rect = hudLayoutRect(renderer, t);
    drawUiScaled(renderer, rm, t,
                 [&](SDL_Renderer *r) { g_hud->draw(r, &rect); });
    if (g_hud->hasPopup())
      drawUiScaled(renderer, rm, t,
                   [&](SDL_Renderer *r) { g_hud->drawPopup(r, &rect); });
    // A dialog (new exhibit name, are you sure) over everything, centred
    if (g_hud->hasDialog())
      drawUiScaled(renderer, rm, t,
                   [&](SDL_Renderer *r) { g_hud->drawDialog(r, &rect); });
    return;
  }
  // Centred at 800x600, exactly like the menus
  drawUiScaled(renderer, rm, t, [&](SDL_Renderer *r) {
    SDL_Rect viewport = {static_cast<int>(std::lround(t.offsetX / t.scale)),
                         static_cast<int>(std::lround(t.offsetY / t.scale)),
                         UI_LAYOUT_W, UI_LAYOUT_H};
    SDL_SetRenderViewport(r, &viewport);
    SDL_Rect layoutRect = {0, 0, UI_LAYOUT_W, UI_LAYOUT_H};
    g_hud->draw(r, &layoutRect);
    SDL_SetRenderViewport(r, nullptr);
  });
  // A drop-down list goes over everything, text included
  if (g_hud->hasPopup())
    drawUiScaled(renderer, rm, t, [&](SDL_Renderer *r) {
      SDL_Rect viewport = {static_cast<int>(std::lround(t.offsetX / t.scale)),
                           static_cast<int>(std::lround(t.offsetY / t.scale)),
                           UI_LAYOUT_W, UI_LAYOUT_H};
      SDL_SetRenderViewport(r, &viewport);
      SDL_Rect layoutRect = {0, 0, UI_LAYOUT_W, UI_LAYOUT_H};
      g_hud->drawPopup(r, &layoutRect);
      SDL_SetRenderViewport(r, nullptr);
    });
  // A dialog (new exhibit name, are you sure) over everything
  if (g_hud->hasDialog())
    drawUiScaled(renderer, rm, t, [&](SDL_Renderer *r) {
      SDL_Rect viewport = {static_cast<int>(std::lround(t.offsetX / t.scale)),
                           static_cast<int>(std::lround(t.offsetY / t.scale)),
                           UI_LAYOUT_W, UI_LAYOUT_H};
      SDL_SetRenderViewport(r, &viewport);
      SDL_Rect layoutRect = {0, 0, UI_LAYOUT_W, UI_LAYOUT_H};
      g_hud->drawDialog(r, &layoutRect);
      SDL_SetRenderViewport(r, nullptr);
    });
}

// ----------------------------------------------------------------------------
// The map: fences laid, exhibits named, tank walls bulldozed
// ----------------------------------------------------------------------------
static SDL_Cursor *g_arrowCursor = nullptr, *g_bulldozeCursor = nullptr, *g_tankWallCursor = nullptr;

static int g_mouseX = 0, g_mouseY = 0;

// What a fence drag or bulldozer click did: charge it, name new exhibits,
// ask before draining a tank. What's placed is booked by its kind, as
// zoo.exe placeObject 0x4d9088 books it: an animal on Animal Purchases
// (0x4e1fde), a keeper, guide or maintenance worker on Employee Wages
// (rows 11-13: 0x5ad038, 0x48bd8b, 0x483d34), anything else on
// Construction (ledgerAdd 0x4d9250), then spent (spendCash 0x41ef68)
static void handleToolResult(const World::ToolResult &r, ResourceManager *rm) {
  if (r.cost > 0)
    g_sim.spend(ZooSim::Construction, r.cost);
  // (a tank's walls dragged: their price, or money back - zoo.exe 0x489e6c)
  if (r.tankWalls != 0)
    g_sim.spend(ZooSim::Construction, r.tankWalls);
  // A new exhibit: exhibit.wav, then its name asked
  if (!r.newExhibits.empty())
    Sound::get().play("sounds/exhibit", 100);
  auto nameThem = [rm](const std::vector<int> &made) {
    for (int id : made) {
      const Fences::Exhibit *ex = g_world->getFences().exhibit(id);
      if (!ex)
        continue;
      // "Exhibit 1", "Tank 2": the count of every exhibit made (zoo.exe
      // habitatDefaultName 0x44df34; the name asked, habitatNameAsk 0x44e43e)
      std::string initial = rm->getString(ex->tank ? 6209 : 6208) + " " + std::to_string(id);
      g_hud->askName(rm->getString(158), initial, [id](const std::string &name) {
        std::string n = name;
        if (n.empty())
          if (const Fences::Exhibit *e = g_world->getFences().exhibit(id))
            n = e->name;
        int gate = g_world->getFences().finishExhibit(id, n, g_world->getMap());
        if (gate > 0)
          g_sim.spend(ZooSim::Construction, gate);
        // (then its staff access check: zoo.exe 0x44e4eb)
        g_world->getStaff().exhibitAccess(id, g_world->getMap(), g_world->getFences());
      });
    }
  };
  // zoo.exe 0x44e389: a tank closed round things it can't hold asks first
  // (tankFillBlocked 0x485aa7, 10552 "To fill the tank, some objects or
  // animals must be sold or removed from the tank."). Yes: they're taken out
  // and it fills (tankEmptyContents 0x458c2b), then it's named. No: zooView
  // +0x455 set, so the placing ends with undoLastAction 0x4de527 (0x5099da,
  // 0x4d8fce): the walls just laid taken back, their money returned
  if (r.askTankFill >= 0) {
    const int tank = r.askTankFill;
    const std::vector<int> made = r.newExhibits;
    g_hud->popupYesNo(
        rm->getString(10552),
        [tank, made, nameThem, rm] {
          if (float refund = g_world->clearTankForFill(tank); refund > 0)
            g_sim.earn(ZooSim::Recycling, refund);
          updateHudClock(rm);
          nameThem(made);
        },
        [rm] {
          undoAndBook();
          updateHudClock(rm);
        });
  } else {
    nameThem(r.newExhibits);
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
  // A building or tank filter bulldozed: "Are you sure you want to sell
  // %s?" first, its refund on Recycling Benefit (zoo.exe removeObject
  // 0x50a0b0 -> bookRemovalRefund 0x4f9329)
  if (!r.askSell.empty())
    g_hud->askConfirm(r.askSell, [rm] {
      if (float refund = g_world->confirmSell(); refund > 0)
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
  // A building placed: Zoo Status redone at once (zoo.exe 0x4516a9 -> 0x41739a)
  if (r.buildingPlaced)
    g_hud->forceZooStatus();
  // A held guest put down: selected, its panel open again (zoo.exe
  // placeObject 0x4d923c: ui_main_4b2322(it, 1), 0x431712)
  if (r.guestDown >= 0)
    g_hud->showGuest(r.guestDown);
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
  // (a DRT base: its own panel - zoo.exe 0x5b66b1, a building of type 8031
  // opens drt.lyt, ui_drt_48cd7a)
  if (r.staff >= 0 && r.wage == 0) {
    const Staff::Member *m = g_world->getStaff().member(r.staff);
    if (m && g_world->getStaff().types()[m->type].kind == Staff::Kind::Helicopter)
      g_hud->showDrt(r.staff);
    else
      g_hud->showStaff(r.staff);
  }
  if (r.wage > 0 || r.staff >= 0) {
    UiGameScreen::ZooInfo info = g_hud->zooInfo();
    info.staffCount = static_cast<int>(g_world->getStaff().members().size());
    g_hud->setZooInfo(info);
  }
  // The money shown changes at once (as the original's)
  updateHudClock(rm);
}

// The right button on the map (zoo.exe: the zoo view's class slot 0x5c,
// 0x4f96fd -> 0x4f9621(0) - the right button, slot 0x58 its press, both
// (x, y, flags)): the bulldozer out (+0x3f4) goes first and alone, its slot
// 0x1c (0x509870): a drag under way taken back, the tool kept; no drag, the
// bulldozer put away (uiTurnOff 0x404, Clear Objects off). Else a drag
// taken back, or what's being placed let go (endPlacing 0x443df7)
static void rightClickOnMap() {
  if (!g_world || !g_hud)
    return;
  if (g_hud->bulldozerOn()) {
    if (!g_world->cancelTool()) {
      g_hud->bulldozerOff();
      g_world->setBulldozer(false);
    }
    return;
  }
  if (!g_world->cancelTool())
    g_hud->dropPicks();
}

// The terraform account settled: paid for (Construction) if the zoo can,
// else put back as it was (zoo.exe 0x4da2e0 - from teraform_Panel_hide
// 0x4da363 and the Terrain tabs' clicks, buldhab_PaintTerrainTab_click
// 0x4dcb50, buldhab_TerraformTab_click 0x4dd68c: the cost, 0x4d9c0f, none
// or within the cash - accepted, 0x4da19c; else undone, 0x6097ef, and 3363
// said in the OK dialog with the exclaim icon, showDialog 0xd23)
static void settleTerrain(ResourceManager *rm) {
  TerrainTool &tt = g_world->getTerrainTool();
  float c = tt.cost();
  if (!g_sim.canAfford(c)) {
    tt.undo();
    g_hud->tell(rm->getString(3363));
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
  UiTransform t = hudTransform(renderer);
  // Guests: their admissions on the books, the zoo's counts and the HUD's
  // guest bar
  {
    Guests &gs = g_world->getGuests();
    // (zoo.exe guestArrivalCheck 0x424375: the rating plus the marketing's
    // bonus - twice after a keeper's "Susan Kittleson" / "Chuck Frizelle"
    // cheat, 0x6392c1 at 0x42443d; 30 more after an exhibit named
    // "wonderland", 0x6392c0 at 0x42444d)
    g_world->setEconomy(static_cast<int>(g_sim.rating()) + g_hud->marketingBenefit() * (RenameCheats::marketingTwice() ? 2 : 1) +
                            (RenameCheats::flags().wonderland ? Original::Cheat::wonderlandArrivalBonus : 0),
                        g_hud->zooInfo().admission);
    int came = 0;
    double paid = gs.takeIncome(came);
    if (double given = gs.takeDonations(); given > 0) {
      g_sim.earn(ZooSim::Donations, given);
      updateHudClock(rm);
    }
    if (double paid = g_world->getShows().takeIncome(); paid > 0) {
      g_sim.earn(ZooSim::ShowIncome, paid);
      updateHudClock(rm);
    }
    for (const Shows::Message &m : g_world->getShows().takeMessages())
      g_hud->postMessage(m.text, m.kind, m.animal >= 0 ? UiGameScreen::Subject::Animal : UiGameScreen::Subject::None, m.animal);
    // (the books' rows: Concessions 0x42df22, Admission income 0x4f7da4,
    // Admissions - a count - 0x4f7e2f, Donations 0x42ebbe)
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
    // A guest's own news ("%s is drowning!", "%s has drowned!": zoo.exe 0x4807c2)
    for (const Guests::Notice &n : gs.takeNotices())
      g_hud->postMessage(n.text, n.kind, UiGameScreen::Subject::Guest, n.guest);
    UiGameScreen::ZooInfo info = g_hud->zooInfo();
    int animals = static_cast<int>(g_world->getAnimals().members().size()), exhibits = 0;
    for (const Fences::Exhibit &e : g_world->getFences().exhibits())
      exhibits += e.named ? 1 : 0;
    int guests = static_cast<int>(gs.guests().size());
    // Zoo Status' counts (zooStatusRefresh 0x53002b): attractions are the
    // things whose type's [Satisfies] list holds "fun"
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
    // (the main bar's meters, zoo.exe ZTGameMgr::update 0x435055: once a
    // second animal happiness, guest happiness and the rating, each -100
    // to 100 shown 0 to 100 - set below with the rating's counts they're
    // read from)
  }
  // The zoo rating, every 4 s (zoo.exe 0x41f881 / 0x41fcc1), each term in
  // whole numbers: 15 x the healthy share of its animals; 10 x its kinds in
  // exhibits out of cSpeciesAvailable (economy.cfg: 20; no more than that
  // many count); (avg happiness + 100) x 25 / 200 for
  // animals and for guests; its value (cash + animals', objects', fences'
  // and path tiles' prices, 0-30000) / 3000; 5% of the research done
  // (cost-0 programs count; all of it with none to do); esthetics (0-10:
  // objects', fences' and path tiles' EstheticBonus over the tiles objects
  // stand on - a new Death Mountain zoo: 2322 / ~740 = 3, so 37, as
  // measured); less 50 while an animal is loose,
  // 2 less a day after (cEscapedAnimalChange / Time); 0-100. Also once
  // straight after a zoo is loaded or started, paused or not (zoo.exe
  // worldAfterLoad 0x4534e6, the world's class slot 0x40: every entity's
  // slot 0x70 and the lists rebuilt, each animal's exhibit rated - 0x45348c
  // -> animalRateExhibit 0x444827, ours on demand - and the fences' states,
  // then, with a game manager, zooRatingTerms 0x41fcc1)
  {
    static float ratingClock = 0;
    static double lastEscape = -1e9;
    static Uint32 lastTick = SDL_GetTicks();
    Uint32 nowTick = SDL_GetTicks();
    ratingClock += g_world->isPaused() ? 0.0f : (nowTick - lastTick) / 1000.0f;
    lastTick = nowTick;
    if (ratingClock >= 4.0f || g_rateNow) {
      ratingClock = 0;
      g_rateNow = false;
      const Animals &an = g_world->getAnimals();
      int n = static_cast<int>(an.members().size());
      std::set<int> kinds;
      long sumH = 0;
      int sick = 0;
      long animalValue = 0;
      for (const Animals::Member &m : an.members()) {
        // (the kinds: every animal in a habitat, its species once each -
        // ratingKindsCount 0x41f0a6, the species +0x1ec put in a set)
        if (m.exhibit >= 0)
          kinds.insert(m.type);
        sumH += static_cast<long>(m.happiness);
        sick += m.sick ? 1 : 0;
        animalValue += an.types()[m.type].cost;
      }
      int r = 0;
      long zooValue = 0; // (the value term before it's kept to 0-30000)
      if (n > 0)
        r += 15 * (n - sick) / n;
      int s = static_cast<int>(kinds.size());
      if (const int avail = Original::economy().speciesAvailable; s > 0 && avail > 0)
        r += 10 * std::min(s, avail) / avail;
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
        // (the same count is this month's books row 25, the guests in the
        // zoo: cleared, then 1 a guest, 0x41f8ad / 0x41f669)
        g_sim.countGuests(k);
        // The main bar's guest meter (zoo.exe ZTGameMgr::update 0x435055 ->
        // 0x41d15d, GuestStatus 1013): this average (+0x70), (it + 100) x
        // 100 / 200, 0 with no guests counted (+0x54)
        if (auto *bar = dynamic_cast<UiStatusImage *>(g_hud->getElementById(1013)))
          bar->setValue(Original::mainBarMeter(avgG, k));
      }
      r += (avgG + 100) * 25 / 200;
      // Value: cash, the animals', every object's (the map's own too) and
      // every fence's price
      {
        static std::unordered_map<std::string, int> priceOf;
        if (priceOf.empty())
          for (const CatalogItem &item : ItemCatalog::get().all())
            priceOf[item.file] = item.cost;
        // (the cash rounded, 0x41f881's fistp, not cut down)
        long v = std::lrint(g_sim.cash()) + animalValue;
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
        // (tank filters too: every 'objects' entity's type cost, slot 0xa4 -
        // ZTTankFilterType's is its +0x100 like the scenery's; staff, whose
        // unit types have none, add nothing)
        for (const Fences::Filter &f : fn.filters())
          v += fn.filterType(f.file).cost;
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
        zooValue = v;
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
        {
          // (zooRatingTerms 0x41fcc1: x 10, kept to 0-100, x -0.1 rounded
          // to the nearest - halves to even - and taken off)
          const float x = std::clamp(static_cast<float>(e) * Original::ratingEstheticScale, 0.0f,
                                     Original::ratingEstheticMax);
          g_esthetic = -static_cast<int>(std::nearbyint(double(x) * double(Original::ratingEstheticBack)));
        }
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
      // (the main bar's rating meter 1015, zoo.exe ZTGameMgr::update
      // 0x435055 -> mainBarRating 0x41d22f, once a second)
      if (auto *zoo = dynamic_cast<UiStatusImage *>(g_hud->getElementById(1015)))
        zoo->setValue(rating);
      // (the main bar's animal meter, [AnimalStatus] 1011 in main.lyt:
      // zoo.exe 0x41d08b sets 0x3f3 once a second from ZTGameMgr::update
      // 0x435055 - game manager +0x6c, (h + 100) x 100 / 200, 0 with no
      // animals, +0x30; the guests' 1013 is 0x41d15d from +0x70)
      if (auto *bar = dynamic_cast<UiStatusImage *>(g_hud->getElementById(1011)))
        bar->setValue(Original::mainBarMeter(n ? static_cast<int>(sumH / n) : 0, n));
      UiGameScreen::ZooInfo info = g_hud->zooInfo();
      if (info.rating != rating || info.value != zooValue) {
        info.rating = rating;
        info.value = zooValue; // (Finances' Zoo Value: zoo.exe 0x52fa39, row 15)
        g_hud->setZooInfo(info);
      }
    }
  }
  for (const Animals::Notice &n : g_world->getAnimals().takeNotices())
    g_hud->postMessage(n.text, n.kind, n.animal >= 0 ? UiGameScreen::Subject::Animal : UiGameScreen::Subject::None,
                       n.animal);
  // A keeper with no way to its exhibit (zoo.exe 0x4e85e8: 11004 / 11001)
  // (ours: the message's place is the keeper; the original's is the
  // habitat's tile, 0x410349)
  for (const Staff::Notice &n : g_world->getStaff().takeNotices())
    g_hud->postMessage(n.text, n.kind, n.member >= 0 ? UiGameScreen::Subject::Staff : UiGameScreen::Subject::None,
                       n.member);
  // zoo.exe 0x49340d: a tank too murky to see into (10549, priority 0)
  for (int id : g_world->getFences().takeMurkyTanks())
    if (const Fences::Exhibit *tank = g_world->getFences().exhibit(id)) {
      std::string s = rm->getString(10549);
      if (size_t at = s.find("%s"); at != std::string::npos)
        s.replace(at, 2, tank->name);
      g_hud->postMessage(s, 0);
    }
  // zoo.exe 0x459d94: a tank left without its water by what's in it (10550,
  // on the message bar as a warning - 0x49cec0 with 5, as 10002)
  for (const std::string &what : g_world->getFences().takeFillRefusals()) {
    std::string s = rm->getString(10550);
    if (size_t at = s.find("%s"); at != std::string::npos)
      s.replace(at, 2, what);
    g_hud->showMessage(s);
  }
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
      g_hud->setTerraformCost(g_sim.canAfford(c) ? c : -c);
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
    g_world->setStaffTool(staffType, g_hud->staffToolFacing());
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
  // exhibit (a dinosaur as its egg: Animals::adopt)
  {
    bool female = false;
    std::string file = g_hud->bulldozerOn() ? "" : g_hud->animalToolFile(female);
    Animals &an = g_world->getAnimals();
    int type = file.empty() ? -1 : an.typeOfFile(file);
    g_world->setAnimalTool(type, female, g_hud->animalToolFacing());
  }
  // The Paths tab's walkway line
  g_hud->setWalkwayLabel(g_world->getBuildHeight() > 0
                             ? "Walkway: +" + std::to_string(g_world->getBuildHeight())
                             : std::string("Walkway: ground"));
  // The bulldozer's cursor while it's picked
  {
    SDL_Cursor *want = g_world->isBulldozing() && g_bulldozeCursor ? g_bulldozeCursor : g_arrowCursor;
    // (a tank wall's top, or dragging it: the up/down arrows - zoo.exe
    // 0x433b34 cursor slot 4)
    if (!g_world->isBulldozing() && g_tankWallCursor && (g_world->overTankWallTop() || g_world->isAdjustingTank()))
      want = g_tankWallCursor;
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
  // The gate the Exhibit List picked: the selection arrow over it, till the
  // list shuts or something else is selected (#266)
  g_world->getFences().selectedGate =
      g_world->getAnimals().selected < 0 && g_world->getGuests().selected < 0 && g_world->getStaff().selected < 0
          ? g_hud->selectedGateExhibit()
          : -1;
  // The tank filter, in the fence list, is placed like an object
  g_world->setFilterTool(!g_hud->bulldozerOn() && !file.empty() &&
                         g_world->getFences().selectFilter(file));
  g_world->setBulldozer(g_hud->bulldozerOn());
  g_world->setDate(g_sim.dayOfMonth(), g_sim.month(), g_sim.year());
  // addition (Features::celebrations): the day's news, flyovers and colours
  Celebrations::update(*g_world, g_sim.dayOfMonth(), g_sim.month(), g_sim.year(),
                       [](const std::string &line) { g_hud->postMessage(line, 0); });

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
      g_world->cash = g_sim.unlimited ? 1e300 : g_sim.cash();
      handleToolResult(g_world->mouseDown(in.x, in.y), rm);
    } else if (in.event == InputEvent::LEFT_RELEASE) {
      g_world->cash = g_sim.unlimited ? 1e300 : g_sim.cash();
      handleToolResult(g_world->mouseUp(in.x, in.y), rm);
    } else if (in.event == InputEvent::RIGHT_CLICK) {
      rightClickOnMap();
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
  if (!w.empty() && w[0] == "mods") {
    std::string target;
    for (size_t n = 2; n < w.size(); n++) { if (!target.empty()) target += ' '; target += w[n]; }
    if (target.size() >= 2 && target.front() == '"' && target.back() == '"') target = target.substr(1, target.size() - 2);
    if (!target.empty() && w[1] == "source") return rm->archiveOf(target);
    if (!target.empty() && (w[1] == "why" || w[1] == "on" || w[1] == "off")) {
      std::string folder;
      for (const auto &mod : Mods::list()) if (mod.folder == target || mod.id == target) { folder = mod.folder; break; }
      if (folder.empty()) return "No installed mod '" + target + "'.";
      if (w[1] == "why") { const auto reason = Mods::offReason(folder); return reason.empty() ? "Optional: can be switched off." : reason; }
      return Mods::setEnabled(folder, w[1] == "on") ? "Switch changed." : Mods::lastError();
    }
    std::string out;
    for (const auto &mod : Mods::list()) {
      out += (mod.enabled ? "[On] " : "[Off] ") + mod.folder;
      const auto why = mod.enabled ? Mods::offReason(mod.folder) : "";
      if (!why.empty()) out += " | " + why;
      out += "\n";
    }
    return out;
  }
  if (!w.empty() && (w[0] == "polishcheck" || w[0] == "polishdemo")) {
    if (g_hud) return "Run this fixture in the Lab Test Zoo, not a player zoo.";
    extern std::string runBuildingPolishChecks(World &, ResourceManager *, bool);
    return runBuildingPolishChecks(*g_world, rm, w[0] == "polishdemo");
  }
  // addition: developer preview of the visual clock; never saved to settings.
  if (!w.empty() && w[0] == "lighting" && g_world) {
    if(w.size()!=2)return "lighting <hour 0-24> | lighting auto | lighting lights";
    if(w[1]=="auto") { g_world->getLighting().previewHour=-1; return "Lighting follows zoo time."; }
    if(w[1]=="lights") return g_world->getLighting().describeLights(); // ours: what's lit this frame
    char *end=nullptr;double hour=std::strtod(w[1].c_str(),&end);
    if(!end || *end || !std::isfinite(hour) || hour<0 || hour>24)return "Choose an hour from 0 to 24, or auto.";
    Features::dayNightLighting=true;g_world->getLighting().previewHour=hour;
    return "Lighting preview: "+w[1]+":00. Use lighting auto to resume the cycle.";
  }
  if (!w.empty() && (w[0] == "activitycheck" || w[0] == "activitydemo")) {
    if (g_hud) return "Run this fixture in the Lab Test Zoo, not a player zoo.";
    extern std::string runActivityPolishChecks(World &, ResourceManager *, bool);
    return runActivityPolishChecks(*g_world, rm, w[0] == "activitydemo");
  }
  if (!w.empty() && w[0] == "paritycheck") {
    if (g_hud) return "Run this fixture in the Lab Test Zoo, not a player zoo.";
    extern std::string runParityChecks(World &, ResourceManager *);
    setRenameHooks(rm); // (as a game started has them)
    return runParityChecks(*g_world, rm);
  }
  if(!w.empty() && (w[0]=="originalwrite" || w[0]=="originalexport")) {
    if(g_hud || w.size()!=2)return "Test Zoo only: originalwrite|originalexport new-output.zoo";
    return SaveGame::checkOriginalWriter(*g_world,w[1],w[0]=="originalexport");
  }
  if(!w.empty() && w[0]=="labzoosave") {
    if(g_hud || (w.size()!=3 && w.size()!=4))return "Test Zoo only: labzoosave freeform/map.scn new-diagnostic.zt1save [crowd]";
    if(std::filesystem::exists(w[2]))return "Output already exists; choose a new diagnostic name.";
    if(w.size()==4) {
      auto &guests=g_world->getGuests();auto &map=g_world->getMap();
      std::vector<std::pair<int,int>> paths;
      for(int y=0;y<map.getHeight();++y)for(int x=0;x<map.getWidth();++x)
        if(map.isPath(x,y) && guests.canWalk(x,y))
          for(const auto &o:g_world->getObjects().objects())
            if(o.bought && o.className=="building" && std::abs(o.x-x)+std::abs(o.y-y)<3) {
              paths.push_back({x,y});break;
            }
      if(paths.empty())return "No walkable paths beside placed buildings.";
      const size_t count=std::clamp(std::atoi(w[3].c_str()),1,1000);
      while(guests.guests().size()<count) {
        const size_t before=guests.guests().size();guests.admit(0);
        if(before==guests.guests().size())return "Cannot admit diagnostic guests.";
      }
      auto &crowd=const_cast<std::vector<Guests::Guest>&>(guests.guests());crowd.resize(count);
      for(size_t i=0;i<crowd.size();++i) {
        auto &g=crowd[i];auto [x,y]=paths[i%paths.size()];g.x=x+0.35f+(i%3)*0.15f;g.y=y+0.35f;
        g.showTank=g.showStand=g.showSeat=-1;g.building=g.target=g.guide=-1;
        g.hidden=g.leaving=false;g.path.clear();g.pathAt=0;g.need.clear();g.state=Guests::State::Walking;
        g.happiness=80;g.hunger=(i%3==0)?75:20;g.thirst=(i%3==1)?75:20;
        g.bathroom=g.tired=15;g.playFor=0;g.tickMs=(i%20)*50;
      }
    }
    ZooSim books;books.start(1000000,50);
    UiGameScreen hud(rm,"ui/main.lyt","ui/gamescrn.lyt");
    g_world->reindex();
    if(!SaveGame::save(w[2],*g_world,books,hud,w[1]))return "Could not write diagnostic zoo.";
    return "Saved Test Zoo diagnostic to "+w[2];
  }
  if(!w.empty() && w[0]=="showcrowdsave") {
    if(g_hud || w.size()!=4)return "Test Zoo only: showcrowdsave count source.zt1save output.zt1save";
    const int count=std::clamp(std::atoi(w[1].c_str()),1,1000);
    if(std::filesystem::exists(w[3]))return "Output already exists; choose a new diagnostic name.";
    const std::string base=SaveGame::mapOf(w[2]);
    if(base.empty() || !g_world->loadFreeform(base))return "Cannot load source map.";
    ZooSim books;UiGameScreen hud(rm,"ui/main.lyt","ui/gamescrn.lyt");
    if(!SaveGame::load(w[2],*g_world,books,hud))return "Cannot restore source zoo.";
    auto &guests=g_world->getGuests();auto &f=g_world->getFences();auto &map=g_world->getMap();
    std::vector<std::pair<int,int>> paths;
    for(int y=0;y<map.getHeight();++y)for(int x=0;x<map.getWidth();++x)
      if(map.isPath(x,y) && guests.canWalk(x,y))
        for(const auto &o:g_world->getObjects().objects())
          if(g_world->getShows().isStand(o.id) && std::abs(o.x-x)+std::abs(o.y-y)<8){paths.push_back({x,y});break;}
    if(paths.empty())return "No reachable paths beside grandstands.";
    while(guests.guests().size()<static_cast<size_t>(count))guests.admit(0);
    auto &crowd=const_cast<std::vector<Guests::Guest>&>(guests.guests());crowd.resize(count);
    for(size_t i=0;i<crowd.size();++i) {
      auto &g=crowd[i];auto [x,y]=paths[i%paths.size()];
      g.x=x+0.3f+(i%3)*0.15f;g.y=y+0.3f+(i%2)*0.3f;
      g.showTank=g.showStand=g.showSeat=-1;g.building=g.target=-1;g.hidden=g.leaving=false;
      g.guide=-1;g.path.clear();g.pathAt=0;g.need.clear();g.happiness=80;
      g.hunger=g.thirst=g.bathroom=g.tired=20;g.state=Guests::State::Walking;
      g.anim="stand";g.animTime=0;g.playFor=1+(i%7)*0.15f;g.tickMs=(i%20)*50;
    }
    g_world->reindex();
    if(!SaveGame::save(w[3],*g_world,books,hud,base))return "Could not write diagnostic zoo.";
    return "Saved "+std::to_string(count)+" guests beside grandstands to "+w[3];
  }
  if (!w.empty() && w[0] == "showmanagementcheck") {
    if(g_hud)return "Run this fixture in the Lab Test Zoo, not a player zoo.";
    extern std::string runShowManagementChecks(World &,ResourceManager *);
    return runShowManagementChecks(*g_world,rm);
  }
  if (!w.empty() && (w[0] == "showpolishcheck" || w[0] == "showpolishdemo")) {
    if(g_hud)return "Run this fixture in the Lab Test Zoo, not a player zoo.";
    extern std::string runShowPolishChecks(World &,ResourceManager *,bool);
    return runShowPolishChecks(*g_world,rm,w[0]=="showpolishdemo");
  }
  if (!w.empty() && w[0] == "marinesoaksetup") {
    if(g_hud)return "Test Zoo only; load flat ff01 first.";
    extern std::string prepareMarineSoak(World &,ResourceManager *);
    return prepareMarineSoak(*g_world,rm);
  }
  if (!w.empty() && w[0] == "showstartcheck") {
    if (g_hud) return "Run this fixture in the Lab Test Zoo, not a player zoo.";
    extern std::string runShowStartChecks(World &);
    return runShowStartChecks(*g_world);
  }
  if (!w.empty() && w[0] == "shows")
    return g_world->getShows().command(w);
  if (!w.empty() && w[0] == "awareness")
    return g_world->getAnimals().awarenessReport();
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
           "filters [health]                 list tank filters / set every one's health (0-10)\n"
           "doors                            every building: where it stands, its door tiles, each on a path or not\n"
           "text <id> [id ...]               the game's text for ids (cNameID, cUsedThought...)\n"
           "fences [map]                     the map's fences by type, which are zoo wall, the tiles inside (map: drawn)\n"
           "saveimportcheck                 verify original guests and animal age before advancing Test Zoo time\n"
        "zoofiles                        read every .zoo (maps, the original's saves) as the original does\n"
           "dirty <ex> [purity]              a tank's water purity (default 30)\n"
           "drain <ex> | fill <ex> [salt|fresh]   empty a tank / fill it again\n"
           "tankwalls <ex> <height>          a tank's walls to that height over its floor\n"
           "shows [tank] [species|tricks|add|script|run|cancel]  marine shows\n"
           "awareness                       animal body clearance, destinations and feeding\n"
           "staff | assign <staff#> <ex>     list staff / put one on an exhibit\n"
           "money <n> | day [n] | month [n] | speed <x> | speedstatus\n"
           "paths [on|off]                   path debug overlay (and routes to the log)\n"
           "features | set <name> on|off     our additions on/off (off = original)\n"
           "raise|lower <x> <y> [n]          a grid corner up/down (makes ramps)\n"
           "layroad <x0> <y0> <x1> <y1>      a test path, x first then y\n"
           "flat | terrainids | tiles <+|->   debug views: no heights, terrain ids, tile size\n"
           "hdart [on|off]                   HD art mods (the zoomed-in map art) listed / switched\n"
           "art [fsr|stylized|sharp|smooth]  the art upscaling at work (Game Options > Graphics)\n"
           "shader [<mod/id> on|off] | shader <mod/id> <setting> <value>   shader mods\n"
           "clear";
  }
  // Shader mods (ShaderMods.hpp): listed with their settings, one on or
  // off, a setting set
  if (c == "shader") {
    auto &list = Shaders::all();
    // (a mod's folder can have spaces in its name - "Starter Kit/shine": the
    // shader is the most words that name one, the rest its arguments)
    std::string key = w.size() > 1 ? w[1] : "";
    size_t next = 2;
    std::string joined;
    for (size_t n = 1; n < w.size(); n++) {
      joined += (n > 1 ? " " : "") + w[n];
      for (const Shaders::Shader &s : list)
        if (Utils::string_to_lower(s.key) == Utils::string_to_lower(joined)) {
          key = s.key;
          next = n + 1;
        }
    }
    if (w.size() > next && (w[next] == "on" || w[next] == "off"))
      Shaders::setOn(key, w[next] == "on");
    else if (w.size() > next + 1)
      Shaders::setValue(key, w[next], static_cast<float>(std::atof(w[next + 1].c_str())));
    if (list.empty())
      return "No shader mods switched on (" + Mods::folder() + ")";
    for (const Shaders::Shader &s : list) {
      out += (out.empty() ? "" : "\n") + s.key + "  " + s.stage + "  " + (s.on ? "on" : "off") +
             (s.failed ? "  FAILED: " + s.log : "");
      for (const Shaders::Setting &t : s.settings) {
        char b[160];
        std::snprintf(b, sizeof b, "\n    %-18s %g  (%g-%g)", t.name.c_str(), t.value, t.min, t.max);
        out += b;
      }
    }
    return out;
  }
  // The art upscaling, as Game Options > Graphics picks it
  if (c == "art") {
    if (w.size() > 1)
      Display::setArt(Display::parseArt(w[1]));
    return std::string("art upscaling: ") + Display::artName(Display::art());
  }
  // HD art mods (HdArt.hpp): the mods with an hd/ folder, listed, or all
  // switched on or off - at once, as Game Options > Mods & Updates does
  if (c == "hdart") {
    std::vector<Mods::Mod> hd;
    std::error_code ec;
    for (const Mods::Mod &m : Mods::list())
      if (std::filesystem::is_directory(std::filesystem::path(m.path) / "hd", ec))
        hd.push_back(m);
    if (hd.empty())
      return "No HD art mods in " + Mods::folder();
    if (w.size() > 1 && (w[1] == "on" || w[1] == "off")) {
      for (const Mods::Mod &m : hd)
        Mods::setEnabled(m.folder, w[1] == "on");
      HdArt::refresh();
      Shaders::refresh();
    }
    bool switched = w.size() > 1 && (w[1] == "on" || w[1] == "off");
    for (const Mods::Mod &m : hd)
      out += (out.empty() ? "" : "\n") + m.name + "  " + (switched ? w[1] : m.enabled ? "on" : "off");
    char zb[96];
    std::snprintf(zb, sizeof zb, "\nthe map last drawn at %.2fx%s; layered %d, direct %d", RenderSettings::lastWorldZoom,
                  RenderSettings::lastWorldZoomedIn ? " from its zoomed-in art" : "", RenderSettings::layeredRuns,
                  RenderSettings::layeredDirect);
    return out + (HdArt::active() ? "\n(HD art at work: zoom in)" : "\n(no HD art at work)") + "\n" + HdArt::stats() + zb +
           (Features::mods ? "" : "\n(Load mods is off: none load)");
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
      if (e->tank) {
        // (its heights and the wall pieces round it it owns)
        int walls = 0, edges = 0;
        for (const auto &[pe, pp] : fences.pieces())
          walls += pp.tank == e->id || pp.tank2 == e->id ? 1 : 0;
        for (auto [x, y] : e->tiles)
          for (auto [dx, dy] : {std::make_pair(1, 0), std::make_pair(-1, 0), std::make_pair(0, 1), std::make_pair(0, -1)})
            edges += e->tiles.count({x + dx, y + dy}) ? 0 : 1;
        int x0 = 1 << 20, x1 = -1, y0 = 1 << 20, y1 = -1;
        for (auto [x, y] : e->tiles)
          x0 = std::min(x0, x), x1 = std::max(x1, x), y0 = std::min(y0, y), y1 = std::max(y1, y);
        std::snprintf(b, sizeof b, "   tiles x %d-%d, y %d-%d  floor %d  ground %d  walls %d (top %.0f)  water %.0f%% (level %d)  wall pieces %d of %d",
                      x0, x1, y0, y1, e->floorHeight, e->groundHeight, e->wallSub, e->wallTop(), e->water * 100,
                      fences.waterLevel(*e), walls, edges);
        out += std::string(b) + "\n";
      }
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
    if (randomTile(e, x, y)) {
      bool carnivore=false;
      // The Lab's Food down control uses the same species/custom chow as staff.
      items.addFood(e->id, x, y, std::max({1,e->testAnimals,g_world->getAnimals().countIn(e->id)}),staff.foodFor(e->id,fences,carnivore));
    }
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
  if (c == "saveerror") return SaveGame::lastError();
  if (c == "saveimportcheck") {
    if (g_hud) return "Use the Lab Test Zoo, not a player's zoo.";
    return SaveGame::checkOriginalImport(*g_world);
  }
  if (c == "zoofiles") {
    // Every .zoo the game has (its maps, in the archives) and the original's
    // own saved games, read in the original's order (ZooReader::readExact):
    // which read to their end, and how many records decoded to exactly
    // their stored size
    std::vector<std::pair<std::string, std::vector<uint8_t>>> files;
    for (const std::string &name : rm->listResources("", ".zoo")) {
      int size = 0;
      if (void *bytes = rm->getFileBytes(name, &size)) {
        files.push_back({name, std::vector<uint8_t>(static_cast<uint8_t *>(bytes), static_cast<uint8_t *>(bytes) + size)});
        free(bytes);
      }
    }
    // (the maps folder holds the maps themselves; the archives' copies are
    // the patches' overrides)
    const std::string savesDir = Paths::originalSavesDir();
    for (const char *folder : {"maps", "Saved Games"}) {
      std::error_code ec;
      const std::string dir = std::string(folder) == "maps" ? Paths::gameDir() + folder : savesDir;
      if (dir.empty())
        continue;
      for (const auto &e : std::filesystem::directory_iterator(dir, ec)) {
        std::string ext = e.path().extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
        if (ext != ".zoo")
          continue;
        std::ifstream in(e.path(), std::ios::binary);
        files.push_back({std::string(folder) + "/" + e.path().filename().string(),
                         std::vector<uint8_t>(std::istreambuf_iterator<char>(in), {})});
      }
    }
    int exact = 0, withGame = 0, records = 0, decodedRecords = 0, transient = 0;
    std::string misses;
    // (zoofiles <part of a name>: that file's records that didn't decode)
    const std::string only = w.size() > 1 ? w[1] : "";
    for (auto &[name, bytes] : files) {
      if (!only.empty()) {
        if (name.find(only) == std::string::npos)
          continue;
        AssetBuffer b;
        b.data = bytes.data();
        b.size = bytes.size();
        ZooReader z;
        z.load(b);
        b.data = nullptr;
        b.size = 0;
        std::map<std::string, int> kinds;
        for (const ZooReader::ZooObject &o : z.getObjects())
          if (!o.decoded)
            kinds[o.className + "/" + o.subClass + "/" + o.typeName + " (" + std::to_string(o.payload.size()) + " bytes)"]++;
        std::string out = name + " v" + std::to_string(z.getVersion()) + (z.isExact() ? "" : " (" + z.problem() + ")") + ":\n";
        for (const auto &[k, n] : kinds)
          out += "  " + std::to_string(n) + " x " + k + "\n";
        // (and what it holds of a game: its habitats, money, date)
        char line[256];
        for (const ZooReader::Habitat &hb : z.getHabitats()) {
          snprintf(line, sizeof line, "  habitat \"%s\" at %d,%d gate %d,%d side %d%s", hb.name.c_str(), hb.x, hb.y, hb.gateX,
                   hb.gateY, hb.gateSide, hb.tank ? "" : "\n");
          out += line;
          if (hb.tank) {
            snprintf(line, sizeof line, " tank height %d water %d %s purity %d%s\n", hb.tankHeight, hb.waterLevel,
                     hb.salt ? "salt" : "fresh", hb.purity, hb.show ? " (show)" : "");
            out += line;
          }
        }
        std::map<std::string, int> classes;
        for (const ZooReader::ZooObject &o : z.getObjects())
          classes[o.className]++;
        out += " ";
        for (const auto &[k, n] : classes)
          out += " " + k + " " + std::to_string(n);
        out += "\n";
        if (z.hasGame()) {
          const uint16_t *d = z.getDate();
          snprintf(line, sizeof line, "  cash %.2f, admission %.2f, rating %u, date %u-%02u-%02u\n", z.getCash(), z.getAdmission(),
                   z.getRating(), d[0], d[1], d[3]);
          out += line;
          const int32_t *vw = z.getView();
          snprintf(line, sizeof line, "  view: zoom %d, rotation %d, scroll %d, %d, brush %d\n", vw[0], vw[1], vw[2], vw[3], vw[4]);
          out += line;
        }
        for (const ZooReader::ZooObject &o : z.getObjects())
          if (o.hasAnimal) {
            snprintf(line, sizeof line, "  animal %s \"%s\" %s at %.1f,%.1f happy %d hunger %d health %d energy %d move %d\n",
                     o.typeName.c_str(), o.name.c_str(), o.animal.female ? "F" : "M", o.x / 64.0, o.y / 64.0, o.animal.happiness,
                     o.animal.hunger, o.animal.health, o.animal.energy, o.movementMode);
            out += line;
          }
        for (const ZooReader::ZooObject &o : z.getObjects())
          if (o.decoded && o.hasGuest) {
            snprintf(line, sizeof line, "  guest %s/%s id %u \"%s\" at %.2f,%.2f happy %d hunger %d thirst %d tired %d bathroom %d\n",
                     o.subClass.c_str(), o.typeName.c_str(), o.id, o.name.c_str(), o.x / 64.0, o.y / 64.0,
                     o.guest.happiness, o.guest.hunger, o.guest.thirst, o.guest.energy, o.guest.bathroom);
            out += line;
          }
        return out;
      }
      AssetBuffer b;
      b.data = bytes.data();
      b.size = bytes.size();
      ZooReader z;
      z.load(b);
      b.data = nullptr; // (not its to free)
      b.size = 0;
      // (a record of size 0 is a passing effect - a tank's ripples, water
      // waves - saved as its names alone: nothing to decode)
      int transientHere = 0;
      for (const ZooReader::ZooObject &o : z.getObjects())
        transientHere += o.payload.empty() ? 1 : 0;
      transient += transientHere;
      records += static_cast<int>(z.getObjects().size()) - transientHere;
      decodedRecords += z.decodedCount();
      if (z.isExact())
        exact++;
      if (z.hasGame())
        withGame++;
      else
        misses += "  " + name + ": " + z.problem() + "\n";
      if (z.isExact() && z.decodedCount() != static_cast<int>(z.getObjects().size()) - transientHere) {
        std::map<std::string, int> left;
        for (const ZooReader::ZooObject &o : z.getObjects())
          if (!o.decoded && !o.payload.empty())
            left[o.className]++;
        std::string l;
        for (const auto &[k, n] : left)
          l += " " + k + " " + std::to_string(n);
        misses += "  " + name + " (v" + std::to_string(z.getVersion()) + "): undecoded" + l + "\n";
      }
    }
    return std::to_string(files.size()) + " .zoo files: " + std::to_string(exact) + " read exactly in the original's order (" +
           std::to_string(withGame) + " to their end, with the saved game's state); " +
           std::to_string(decodedRecords) + " of " + std::to_string(records) + " records decoded to exactly their size (and " +
           std::to_string(transient) + " passing effects saved with no body)\n" + misses;
  }
  // The game's own text for ids (names, thoughts - cNameID, cUsedThought), as
  // the language files have them
  if (c == "text") {
    if (w.size() < 2)
      return "text <id> [id ...]";
    for (size_t i = 1; i < w.size(); i++) {
      uint32_t id = static_cast<uint32_t>(std::strtoul(w[i].c_str(), nullptr, 10));
      std::string t = rm->getString(id);
      out += w[i] + ": " + (t.empty() ? "(none)" : t) + "\n";
    }
    return out;
  }
  // The fences on the map by type: how many pieces, which are zoo wall (the
  // wall the inside of the zoo is found from), and how many tiles that makes
  // inside
  if (c == "fences" && w.size() >= 2 && w[1] == "map") {
    // (inside the wall #, outside ., a zoo-wall piece's tile +)
    const WorldMap &m = g_world->getMap();
    std::vector<std::string> rows(static_cast<size_t>(m.getHeight()), std::string(static_cast<size_t>(m.getWidth()), '.'));
    for (int y = 0; y < m.getHeight(); y++)
      for (int x = 0; x < m.getWidth(); x++)
        if (fences.insideZoo(x, y))
          rows[static_cast<size_t>(y)][static_cast<size_t>(x)] = '#';
    for (const auto &[e, p] : fences.pieces())
      if (fences.types()[static_cast<size_t>(p.type)].zooWall && p.ownerY >= 0 && p.ownerY < m.getHeight() && p.ownerX >= 0 &&
          p.ownerX < m.getWidth())
        rows[static_cast<size_t>(p.ownerY)][static_cast<size_t>(p.ownerX)] = '+';
    for (size_t y = 0; y < rows.size(); y++)
      out += (y < 10 ? " " : "") + std::to_string(y) + " " + rows[y] + "\n";
    return out;
  }
  if (c == "fences" && w.size() < 2) {
    std::map<int, int> count;
    for (const auto &[e, p] : fences.pieces())
      count[p.type]++;
    for (const auto &[t, n] : count) {
      const FenceType &ft = fences.types()[static_cast<size_t>(t)];
      out += ft.key + " (" + ft.name + "): " + std::to_string(n) + (ft.zooWall ? "  zoo wall" : "") + (ft.tank ? "  tank" : "") + "\n";
    }
    int in = 0, x0 = 1 << 20, y0 = 1 << 20, x1 = -1, y1 = -1;
    const WorldMap &m = g_world->getMap();
    for (int y = 0; y < m.getHeight(); y++)
      for (int x = 0; x < m.getWidth(); x++)
        if (fences.insideZoo(x, y)) {
          in++;
          x0 = std::min(x0, x), y0 = std::min(y0, y), x1 = std::max(x1, x), y1 = std::max(y1, y);
        }
    out += std::to_string(in) + " of " + std::to_string(m.getWidth()) + "x" + std::to_string(m.getHeight()) +
           " tiles inside the zoo wall";
    if (in)
      out += ", from " + std::to_string(x0) + "," + std::to_string(y0) + " to " + std::to_string(x1) + "," + std::to_string(y1);
    return out;
  }
  if (c == "doors") {
    char line[160];
    for (const PlacedObjects::Object &o : g_world->getObjects().objects()) {
      if (o.fence || o.className != "building")
        continue;
      snprintf(line, sizeof line, "%s at %.2f,%.2f facing %d:", o.typeName.c_str(), o.x, o.y, o.facing);
      out += line;
      std::vector<std::pair<int, int>> doors = g_world->getGuests().doorTiles(o);
      if (doors.empty())
        out += " no entrpos";
      for (const auto &[x, y] : doors) {
        snprintf(line, sizeof line, " %d,%d%s", x, y, g_world->getGuests().canWalk(x, y) ? " (path)" : "");
        out += line;
      }
      out += "\n";
    }
    return out.empty() ? "No buildings." : out;
  }
  if (c == "filters" && w.size() < 2) {
    std::string out;
    static const char *const states[] = {"running", "decayed", "broken", "off"};
    for (const Fences::Filter &f : fences.filters())
      out += f.name + "  at " + std::to_string(f.x) + "," + std::to_string(f.y) + "  tank " + std::to_string(f.tank) +
             "  health " + std::to_string(static_cast<int>(f.health)) + "  " +
             states[static_cast<int>(fences.filterState(f))] + "  sound " + fences.filterSound(f).first + "\n";
    return out.empty() ? "No filters." : out;
  }
  if (c == "filters") {
    int n = 0;
    for (size_t i = 0; i < fences.filters().size(); i++)
      if (Fences::Filter *f = fences.filter(static_cast<int>(i))) {
        f->health = num(1, static_cast<float>(fences.filterType(f->file).decayedHealth));
        n++;
      }
    return std::to_string(n) + " filters set";
  }
  if (c == "drain" || c == "fill") {
    Fences::Exhibit *e = exhibitArg(1);
    if (!e || !e->tank)
      return "Which tank?";
    if (c == "drain")
      fences.drainExhibit(e->id);
    else
      fences.fillExhibit(e->id, w.size() > 2 ? w[2] != "fresh" : e->salt);
    return e->name + (c == "drain" ? ": drained" : ": filled");
  }
  if (c == "dirty") {
    Fences::Exhibit *e = exhibitArg(1);
    if (!e || !e->tank)
      return "Which tank?";
    e->purity = std::clamp(num(2, 30), 0.0f, 100.0f);
    return e->name + ": water purity " + std::to_string(static_cast<int>(e->purity));
  }
  if (c == "tankwalls") {
    // A tank's walls to a height (units over its floor), a step at a time
    // as Tank Adjustment does (its limits too: 1 up to maximumTankHeight);
    // the water follows them up
    Fences::Exhibit *e = exhibitArg(1);
    if (!e || !e->tank)
      return "Which tank?";
    const int want = static_cast<int>(num(2, static_cast<float>(e->wallSub)));
    while (e->wallSub < want && fences.canAdjustWall(e->id, 1))
      fences.adjustWall(e->id, 1);
    while (e->wallSub > want && fences.canAdjustWall(e->id, -1))
      fences.adjustWall(e->id, -1);
    return e->name + ": walls " + std::to_string(e->wallSub) + " over the floor (rim " +
           std::to_string(static_cast<int>(e->wallTop())) + ", ground " + std::to_string(e->groundHeight) + ")";
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
    const bool none[SDL_SCANCODE_COUNT] = {};
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
        const bool on = w[2] == "on" || w[2] == "1" || w[2] == "true";
        if (std::string(f.name) == "mods") {
          if (!Mods::setMasterEnabled(on)) return Mods::lastError();
        } else {
          *f.value = on;
        }
        if (std::string(f.name) == "fencemodes")
          g_world->cancelTool();
        if (std::string(f.name) == "allunlocked") {
          ItemCatalog::get().setMode(g_currentMapPath.rfind("scenario:", 0) == 0, Features::allUnlocked);
          ItemCatalog::get().setMonth(unlockMonthFor(monthsPlayed()));
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
    g_speedClock.clearPending();
    if (g_hud) g_hud->setGameSpeed(static_cast<int>(g_gameSpeed));
    char b[64];
    std::snprintf(b, sizeof b, "Game speed x%.1f", g_gameSpeed);
    return b;
  }
  if(c=="speedstatus") {
    std::ostringstream out;out<<"Requested "<<g_gameSpeed<<"x; pending "<<g_speedClock.pending()
      <<" game seconds; discarded catch-up "<<g_speedClock.discarded()<<" game seconds.";
    return out.str();
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
      else if (g_world->isBridgePlan())
        lines.push_back({"Bridge: the ground is too uneven for a path here", white});
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
    // (clear of the tooltip shown - the tool's own hint over the map, a
    // button's over the bars - which is under the cursor: above the cursor
    // then, or to its left; drawn over it, the two boxes hid each other)
    if (g_hud && g_hud->shownTip.w > 0) {
      const UiTransform ht = hudTransform(renderer);
      const SDL_Rect tip = {static_cast<int>(ht.offsetX + g_hud->shownTip.x * ht.scale),
                            static_cast<int>(ht.offsetY + g_hud->shownTip.y * ht.scale),
                            static_cast<int>(g_hud->shownTip.w * ht.scale), static_cast<int>(g_hud->shownTip.h * ht.scale)};
      if (SDL_HasRectIntersection(&box, &tip)) {
        box.y = g_mouseY - static_cast<int>(12 * t.scale) - box.h;
        if (SDL_HasRectIntersection(&box, &tip))
          box.x = g_mouseX - static_cast<int>(16 * t.scale) - box.w;
      }
    }
    SDL_BlendMode was;
    SDL_GetRenderDrawBlendMode(renderer, &was);
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer, 34, 29, 15, 240);
    ZT_RenderFillRect(renderer, &box);
    SDL_SetRenderDrawColor(renderer, 255, 217, 90, 255);
    ZT_RenderDrawRect(renderer, &box);
    SDL_SetRenderDrawBlendMode(renderer, was);
    int y = box.y + pad;
    for (SDL_Texture *m : tex) {
      if (!m)
        continue;
      int w = 0, h = 0;
      rm->getTextSize(m, &w, &h);
      SDL_Rect r = {box.x + pad, y, w, h};
      ZT_RenderCopy(renderer, m, nullptr, &r);
      y += h;
    }
  }
  if (g_world->hoverCost() < 0)
    return;
  // As the original: "$70" over a piece, "-$630" dragging (money going
  // out), "$0" where none can go
  // (a tank's walls dragged: 0x489f53 passes the price negated - "-$206"
  // up, money back coming down from above where they started)
  std::string label = ((g_world->isDraggingFence() || g_world->isDraggingPath() || g_world->getStaffTool() >= 0 ||
                        (g_world->isAdjustingTank() && !g_world->wallDragRefund())) &&
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
  ZT_RenderCopy(renderer, text, nullptr, &r);
}

extern float g_ZoomLevel; // World.cpp
static UiAction hudInputs(SDL_Renderer *renderer, std::vector<Input> inputs);

// A frame's input in a game: the HUD first, then the map - but not a click
// that landed on the interface (decided before the HUD handled it: a
// window's X closes it, and the click mustn't then reach the tool held)
static UiAction gameInputs(SDL_Renderer *renderer, ResourceManager *rm, const std::vector<Input> &inputs) {
  bool dialogBefore = g_hud->hasDialog();
  g_uiClicks.clear();
  UiTransform ut = hudTransform(renderer);
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
  // (the Options window, open, takes them all - at its own size)
  UiTransform t = g_hud->optionsOpen() ? optionsTransform(renderer) : hudTransform(renderer);
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
        World::raisable(g_world->getPathTool()) && !(SDL_GetModState() & SDL_KMOD_CTRL)) {
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

// The main menu's Options window (the optionswindow extra)
static UiOptions *g_menuOptions = nullptr;
static constexpr int kMainMenuOptions = 7117;
// The main menu with Game Options (the optionswindow extra; off: as the
// original) in Get New Zoo Tycoon Items' place - its line, font and spacing:
// that entry's online service is gone, and the Mod Menu it opened for now is
// to come back elsewhere
static std::string mainMenuWithOptions(ResourceManager *rm) {
  std::string t = UiUpdate::withStatus(rm->getGameText("ui/startup.lyt"));
  if (!Features::optionsWindow)
    return t;
  size_t s = t.find("[ZooItems]");
  if (s == std::string::npos)
    return t;
  size_t e = t.find("\n[", s + 1);
  std::string entry = t.substr(s, e == std::string::npos ? std::string::npos : e + 1 - s);
  auto swap = [&entry](const std::string &from, const std::string &to) {
    size_t at = entry.find(from);
    if (at != std::string::npos)
      entry.replace(at, from.size(), to);
  };
  swap("textid = 7114", "textid = 0"); // (its words: hookMainMenu)
  swap("id = 7114", "id = " + std::to_string(kMainMenuOptions));
  swap("helpid = 7124", "helpid = 0");
  swap("action=1", ";action=1");
  swap("target=35", ";target=35");
  t.replace(s, e == std::string::npos ? std::string::npos : e + 1 - s, entry);
  // A section's key set, whatever its spacing ("x = 295", "x=center": a
  // checkout's loose ui/startup.lyt places the lines by hand)
  auto setKey = [&t](const std::string &section, const std::string &key, const std::string &value) {
    size_t from = t.find("[" + section + "]");
    if (from == std::string::npos)
      return;
    size_t to = t.find("\n[", from + 1);
    size_t at = from;
    while ((at = t.find('\n', at)) != std::string::npos && (to == std::string::npos || at < to)) {
      at++;
      size_t eq = t.find('=', at), end = t.find_first_of("\r\n", at);
      if (eq == std::string::npos || end == std::string::npos || eq > end)
        continue;
      std::string k = t.substr(at, eq - at);
      k.erase(k.find_last_not_of(" \t") + 1);
      if (k == key) {
        t.replace(at, end - at, key + " = " + value);
        return;
      }
    }
  };
  // Game Options and View Game Credits in the big lines' font (7100, as Play
  // Freeform Game), centred, 40 apart after Continue Saved Game (380); Exit
  // stays the small one, below
  for (const char *line : {"ZooItems", "Credits"}) {
    setKey(line, "x", "center");
    setKey(line, "justify", "center");
    setKey(line, "dx", "268");
    setKey(line, "font", "7100");
    setKey(line, "fontsize", "7101");
  }
  setKey("ZooItems", "y", "420");
  setKey("Credits", "y", "460");
  setKey("Exit", "y", "505");
  // (and no action of its own, whatever the file's spacing - "action = 1":
  // Get New Zoo Tycoon Items' target 35, the Mod Menu, was still opening
  // under the window whenever the Mod Menu was switched on; its click opens
  // the window, hookMainMenu)
  setKey("ZooItems", "action", "0");
  setKey("ZooItems", "target", "0");
  return t;
}
static void hookMainMenu(UiLayout *layout, ResourceManager *rm) {
  // Its Version_Label: string 1526 ("Version Number: %s") with the build's
  // version (zoo.exe startup_Panel_show 0x50f1c8: its own ProductVersion)
  if (UiText *v = dynamic_cast<UiText *>(layout->getElementById(7119))) {
    std::string label = rm->getString(1526);
    if (size_t p = label.find("%s"); p != std::string::npos)
      label.replace(p, 2, std::string(Updater::version()) + (Updater::testBuild() ? " (test)" : ""));
    v->setText(label);
    SDL_Log("[Menu] version label: \"%s\"", label.c_str());
  } else {
    SDL_Log("[Menu] no version label (7119) in the layout");
  }
  if (UiButton *b = dynamic_cast<UiButton *>(layout->getElementById(kMainMenuOptions))) {
    // (string 1540 ends in spaces, so it sat off the centre)
    b->setLabel("Game Options");
  }
  if (UiButton *b = dynamic_cast<UiButton *>(layout->getElementById(kMainMenuOptions)))
    b->onClick = [] {
      if (g_menuOptions)
        g_menuOptions->open(false);
    };
  // Continue Saved Game (zoo.exe 0x50ef37, from startup_Panel_show 0x50f1c8):
  // only while zoo.ini [user] lastfile names a file that's there
  // (GetFileAttributesA); otherwise off, and lastfile cleared
  if (UiElement *c = layout->getElementById(static_cast<int>(UiAction::STARTUP_CONTINUE_GAME))) {
    const std::string last = SaveGame::lastFile();
    std::error_code ec;
    const bool there = !last.empty() && std::filesystem::exists(last, ec);
    if (!there && !last.empty())
      SaveGame::setLastFile("");
    c->setDisabled(!there);
  }
}

static void updateScenarioDetails(UiLayout *layout,
                                  ScenarioManager *scenarioManager,
                                  ResourceManager *resourceManager) {
  if (!layout || !scenarioManager || !resourceManager)
    return;
  if (!g_scenarioListBox)
    return;

  // The Play button (50008) is enabled only for a scenario that is open
  // (zoo.exe scenario_ScenarioList_click 0x58e9bb: slot 0x6c disables it
  // first, then scenarioIsOpen 0x58eb28 of the row chosen enables it, 0x68)
  UiElement *playButton = layout->getElementById(50008);
  if (playButton)
    playButton->setDisabled(true);

  int selectedIdx = g_scenarioListBox->getSelectedIndex();
  if (selectedIdx < 0)
    return;

  const ScenarioInfo *scenario = scenarioManager->getScenario(selectedIdx);
  if (!scenario)
    return;

  bool isLocked = scenario->isLocked;
  if (playButton)
    playButton->setDisabled(isLocked);
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
        // The scenario's own words ([desc] lockedstory, a string id or a
        // lang0.dll TEXT name such as SCENARIO_LOCK_STORY_0: zoo.exe
        // scenario_ScenarioList_click 0x58e9bb -> 0x600085 -> 0x4f0264)
        std::string lockedStory;
        if (IniReader *ini = resourceManager->getIniReader(scenario->scenarioPath)) {
          std::string key = ini->get("desc", "lockedstory");
          delete ini;
          while (!key.empty() && std::isspace(static_cast<unsigned char>(key.back())))
            key.pop_back();
          while (!key.empty() && std::isspace(static_cast<unsigned char>(key.front())))
            key.erase(key.begin());
          if (!key.empty() && std::all_of(key.begin(), key.end(), ::isdigit))
            lockedStory = resourceManager->getString(static_cast<uint32_t>(std::atoi(key.c_str())));
          else if (!key.empty())
            lockedStory = langText(key);
        }
        if (!lockedStory.empty()) {
          g_scenarioDescText->setText(lockedStory);
        } else if (!scenario->stateMessage.empty()) {
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
        // The goals' lines as the scenario page gives them (zoo.exe 0x4f004a's
        // list: the time limit, then each goal shown, with its icon - Goals::lines)
        Goals page;
        page.load(resourceManager, scenario->scenarioPath);
        const std::vector<Goals::Line> lines = page.lines();
        objList->setWrapped(!lines.empty());
        for (const Goals::Line &line : lines)
          objList->addItem(line.text, "", line.icon);
        if (lines.empty())
          for (const auto &goal : scenarioManager->loadScenarioObjectives(scenario->scenarioPath))
            objList->addItem(goal);
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

// (zoo.exe scenario_Panel_show 0x4caf09 fills the list: cleared, each
// scenario's name by number with its icon - done, in progress or not done,
// scenarioIsDone 0x4cae6a)
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
      // (zoo.exe scenario_ScenarioList_pick 0x4cb2f8, the layout's
      // handledoubleclicks: a second click on a row presses Play, when it is
      // lit - the handler of the Play button starts open ones only)
      g_scenarioListBox->setDoubleClickAction(UiAction::PLAY_SCENARIO_START);

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

  // Starting Cash (11510) and its Difficulty (11531), from zoo.ini
  g_startingCashText = dynamic_cast<UiText *>(layout->getElementById(11510));
  g_difficultyText = dynamic_cast<UiText *>(layout->getElementById(11531));
  loadStartingCash();
  showStartingCash();
  // Typed in (zoo.exe mapselec_StartingCash_click 0x616414): the currency
  // symbol taken out once (text 22100, 0x5fe02d) and every thousands
  // separator (",", 0x5fe0a9), the rest read as a whole number (0x405218);
  // at MSMaxCash or more, MSMaxCash; at MSMinCash or less, MSMinCash; kept
  // and written back (0x4cb384)
  if (g_startingCashText)
    g_startingCashText->onCommit = [](const std::string &v) {
      std::string text = v;
      const std::string sign = g_cashStrings ? g_cashStrings->getString(22100) : std::string("$");
      if (size_t at = sign.empty() ? std::string::npos : text.find(sign); at != std::string::npos)
        text.erase(at, sign.size());
      for (size_t at; (at = text.find(',')) != std::string::npos;)
        text.erase(at, 1);
      setStartingCash(std::atoi(text.c_str()));
    };
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
static bool g_labModContext = false;
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
  SDL_GetCurrentRenderOutputSize(renderer, &w, &h);
  SDL_Surface *s = SDL_CreateSurface(w, h, SDL_PIXELFORMAT_RGB24);
  bool ok = false;
  std::string written;
  if (s && ZT_RenderReadPixels(renderer, nullptr, SDL_PIXELFORMAT_RGB24, s->pixels, s->pitch) == 0) {
    written = (dir / (std::string(name) + ".jpg")).string();
    ok = IMG_SaveJPG(s, written.c_str(), 75);
    if (!ok) {
      written = (dir / (std::string(name) + ".bmp")).string();
      ok = SDL_SaveBMP(s, written.c_str());
    }
  }
  if (s)
    SDL_DestroySurface(s);
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
static bool g_dialogPaused = false;   // a modal (Game Options, name or question) paused it

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
static bool gameModalOpen() {
  // (the Help window too: zoo.exe help_Panel_show 0x484a6b pauses the game,
  // help_Panel_hide 0x484bc1 lets it go)
  return g_hud && (g_hud->dialogOpen() || g_hud->optionsOpen() || g_hud->isPanelOpen(5) || g_hud->isPanelOpen(38));
}

static void resumeAfterPopup() {
  if (g_popupPaused && g_hud && !gameModalOpen()) {
    g_popupPaused = false;
    setGamePaused(false);
  }
}

// The advisor's messages for the zoo's counts (zoo.exe zooAdvisories
// 0x420347): each message's text number and its kind (1 green, 2 red)
static std::vector<std::pair<int, int>> zooAdvice(const Guests::Census &gc, int animals, int sick) {
  const Original::Economy &econ = Original::economy();
  std::vector<std::pair<int, int>> out;
  if (animals * econ.pctProtestors < sick)
    out.push_back({10009, 2});
  if (gc.guests > 10) {
    if (animals * econ.pctSick < sick)
      out.push_back({10008, 2});
    if (gc.guests * econ.pctHungry < gc.hungry)
      out.push_back({10011, 2});
    if (gc.guests * econ.pctThirsty < gc.thirsty)
      out.push_back({10012, 2});
    if (gc.guests * econ.pctBathroom < gc.bathroom)
      out.push_back({10013, 2});
    if (gc.guests * econ.pctSouvenir < gc.gift)
      out.push_back({10014, 2});
    if (gc.guests * econ.pctTired < gc.tired)
      out.push_back({10015, 2});
    if (gc.guests * econ.pctTrash < gc.trash)
      out.push_back({10016, 2});
    if (econ.highGuestHappy <= gc.happiness)
      out.push_back({10021, 1});
    if (gc.happiness <= econ.lowGuestHappy)
      out.push_back({10022, 2});
  }
  return out;
}

// The goal engine's view of the zoo (zoo.exe 0x41d665) and what its
// triggers do
// A scenario's reward kept for freeform (zoo.exe 0x598cdc): a type not yet
// kept goes into zoo.ini [user] unlockEntity<unlockCount> (unlockCount one
// more), and is made available now
static void keepReward(int nameId) {
  if (nameId <= 0)
    return;
  const int count = Settings::getInt("user", "unlockCount", 0);
  for (int n = 0; n < count; n++)
    if (Settings::getInt("user", "unlockEntity" + std::to_string(n), 0) == nameId)
      return;
  Settings::setInt("user", "unlockEntity" + std::to_string(count), nameId);
  Settings::setInt("user", "unlockCount", count + 1);
  if (const CatalogItem *item = ItemCatalog::get().findName(nameId))
    if (ItemCatalog::get().setAvailable(*item, true) && g_hud)
      g_hud->refreshCatalog();
}

static void applyResearch(const ResearchProgram &p);
// Goal rule 2 (zoo.exe 0x41d869) counts every entity of the world (the vector
// at world +0x80) whose type's name (slot 0x20 of its type, +0x128) is
// nameId: besides the animals, the staff, the objects and fences, and the
// path tiles of that path type (the tutorials count keepers, paths, stands)
static int otherEntitiesNamed(int nameId) {
  if (!g_world || nameId <= 0)
    return 0;
  int n = 0;
  const auto &staffTypes = g_world->getStaff().types();
  for (const Staff::Member &m : g_world->getStaff().members())
    n += m.type >= 0 && m.type < static_cast<int>(staffTypes.size()) && staffTypes[m.type].nameId == nameId ? 1 : 0;
  for (const PlacedObjects::Object &o : g_world->getObjects().objects())
    n += o.nameId == nameId ? 1 : 0;
  WorldMap &map = g_world->getMap();
  const std::vector<std::string> &pathTypes = map.getPathTypes();
  int pathType = -1;
  if (const CatalogItem *item = ItemCatalog::get().findName(nameId))
    if (item->registrySection == "paths")
      for (size_t i = 0; i < pathTypes.size(); i++)
        if (pathTypes[i] == item->registryKey)
          pathType = static_cast<int>(i);
  if (pathType >= 0)
    for (int y = 0; y < map.getHeight(); y++)
      for (int x = 0; x < map.getWidth(); x++)
        n += map.getPathType(x, y) == pathType ? 1 : 0;
  return n;
}

static void setupGoals(ResourceManager *rm) {
  Goals::Hooks h;
  h.measure = [](const Goals::Goal &g) -> int {
    if (!g_world)
      return 0;
    // (the game manager's month +0x15c and year +0x160, both from 0 -
    // measured: February of year 1 reads 1 and 0 in the running original)
    const int month = g_sim.month(), year = g_sim.year() - 1;
    const int months = month + year * 12;
    Animals &an = g_world->getAnimals();
    if (g.rulea == 1) {
      // (not before month arga: 0x41d665)
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
    // The entities' count (zoo.exe 0x41d869 rule 2): each animal whose species
    // (cNameID) is arga (a) and whose subtype - 'm', 'f', 'p' or 'y', the
    // table at 0x639280 - is the one argb names (b); ruleb 0 counts a, 1 b,
    // 2 both, 3 either
    if (g.rulea == 2 && g.ruleb >= 0 && g.ruleb <= 3) {
      int n = 0;
      for (const Animals::Member &m : an.members()) {
        const bool a = an.types()[m.type].nameId == g.arga;
        const bool b = (m.baby ? 3 : m.female ? 1 : 0) == g.argb;
        n += g.ruleb == 0 ? a : g.ruleb == 1 ? b : g.ruleb == 2 ? (a && b) : (a || b);
      }
      // (the entities that are not animals have no subtype: counted by their name)
      if (g.ruleb == 0 || g.ruleb == 3)
        n += otherEntitiesNamed(g.arga);
      return n;
    }
    // The animals counted by what they are (zoo.exe 0x4f0ff2, rule 4; arga
    // the wanted value, argb the second where there is one): 0 family, 1
    // genus, 2 species, 3 location, 4 habitat; 5-9 a pair of them (5 family
    // and location, 6 genus and location, 7 family and habitat, 8 genus and
    // habitat, 9 habitat and location); 10 those in a crate, 11-12 in a crate
    // of that family or genus; 13 eggs, 14-15 eggs of that family or genus;
    // 19 of that expansion (the type's +0x54)
    if (g.rulea == 4 && ((g.ruleb >= 0 && g.ruleb <= 15) || g.ruleb == 19)) {
      int n = 0;
      for (const Animals::Member &m : an.members()) {
        const Animals::Type &t = an.types()[m.type];
        bool hit = false;
        switch (g.ruleb) {
        case 0: hit = t.family == g.arga; break;
        case 1: hit = t.genus == g.arga; break;
        case 2: hit = t.nameId == g.arga; break;
        case 3: hit = t.location == g.arga; break;
        case 4: hit = t.habitatKind == g.arga; break;
        case 5: hit = t.family == g.arga && t.location == g.argb; break;
        case 6: hit = t.genus == g.arga && t.location == g.argb; break;
        case 7: hit = t.family == g.arga && t.habitatKind == g.argb; break;
        case 8: hit = t.genus == g.arga && t.habitatKind == g.argb; break;
        case 9: hit = t.habitatKind == g.arga && t.location == g.argb; break;
        case 10: hit = m.boxed; break;
        case 11: hit = m.boxed && t.family == g.arga; break;
        case 12: hit = m.boxed && t.genus == g.arga; break;
        case 13: hit = m.egg; break;
        case 14: hit = m.egg && t.family == g.arga; break;
        case 15: hit = m.egg && t.genus == g.arga; break;
        case 19: hit = t.expansionId == g.arga; break;
        }
        n += hit ? 1 : 0;
      }
      return n;
    }
    // The exhibits' suitability (zoo.exe 0x4b0c83 rule 5, 0x41da81 rule 6):
    // each exhibit with animals averages its animals' suitability (only
    // those that ruleb picks - 0 every animal, 1 a family, 2 a genus, 3 a
    // species, 4 a location, 5 an era, 6 a habitat kind - of the value arga
    // in rule 5, argb in rule 6; an exhibit with none of them doesn't
    // count). Rule 5: every exhibit counted must average value or more (and
    // one at least); rule 6: at least arga of them. Met: the value; else 0
    if ((g.rulea == 5 || g.rulea == 6) && g.ruleb >= 0 && g.ruleb <= 6) {
      const int want = g.rulea == 5 ? g.arga : g.argb;
      auto keep = [&](const Animals::Member &m) {
        const Animals::Type &t = an.types()[m.type];
        switch (g.ruleb) {
        case 1: return t.family == want;
        case 2: return t.genus == want;
        case 3: return t.nameId == want;
        case 4: return t.location == want;
        case 5: return t.era == want;
        case 6: return t.habitatKind == want;
        default: return true;
        }
      };
      int counted = 0, met = 0;
      for (const Fences::Exhibit &ex : g_world->getFences().exhibits()) {
        if (ex.animals <= 0)
          continue;
        const int s = an.exhibitSuitability(ex.id, keep);
        if (s < 0)
          continue;
        counted++;
        met += s >= g.value ? 1 : 0;
      }
      if (g.rulea == 5)
        return counted > 0 && met == counted ? g.value : 0;
      return met > 0 && met >= std::max(1, g.arga) ? g.value : 0;
    }
    if (g.rulea == 7) // (the month, the year, or both as months)
      return g.ruleb == 0 ? month : g.ruleb == 1 ? year : g.ruleb == 2 ? months : 0;
    if (g.rulea == 3 && g.ruleb == 0) // (the exhibits: habitatMgr's list)
      return static_cast<int>(g_world->getFences().exhibits().size());
    if (g.rulea == 3 && g.ruleb == 1) { // (the exhibits with animals in, 0x412167)
      int n = 0;
      for (const Fences::Exhibit &ex : g_world->getFences().exhibits())
        n += ex.animals > 0 ? 1 : 0;
      return n;
    }
    if (g.rulea == 3 && g.ruleb == 2) { // (the species in exhibits, ratingKindsCount 0x41f0a6)
      std::set<int> kinds;
      for (const Animals::Member &m : an.members())
        if (m.exhibit >= 0)
          kinds.insert(m.type);
      return static_cast<int>(kinds.size());
    }
    // The books (zoo.exe 0x41dd64 rule 0): row ruleb of the lifetime column
    // (arga 2): 14 the net, every booking's sum, 15 the zoo's value
    if (g.rulea == 0 && g.arga == 2 && g.ruleb == 14) {
      double net = 0;
      for (const ZooSim::Month &mo : g_sim.history())
        net += mo.total();
      return static_cast<int>(net);
    }
    if (g.rulea == 0 && g.arga == 2 && g.ruleb == 15)
      return g_hud ? static_cast<int>(g_hud->zooInfo().value) : 0;
    // Research done (zoo.exe 0x61bf58 rule 13): the programs of the ids in
    // arga that are finished (a branch's or category's own id: no scenario
    // uses one)
    if (g.rulea == 13 && g.ruleb == 0) {
      int n = 0;
      for (ResearchBranch &b : Research::get().branches())
        for (ResearchCategory &c : b.categories)
          for (ResearchProgram &p : c.programs)
            if (p.nameId == g.arga && (p.done || p.cost <= 0))
              n++;
      return n;
    }
    // The shows' books (zoo.exe 0x59ef30 rule 15): the sum over the shows
    // (those of species arga, when arga is 1 or more) of 0 last month's
    // income, 1 the income in all, 2 last month's profit (the income less the
    // upkeep), 3 the profit in all, 4 the visitors in all
    if (g.rulea == 15 && g.ruleb >= 0 && g.ruleb <= 4) {
      double sum = 0;
      for (const auto &entry : g_world->getShows().all()) {
        const Shows::Show &s = entry.second;
        if (g.arga >= 1) {
          const int type = an.typeOfFile(s.species);
          if (type < 0 || an.types()[type].nameId != g.arga)
            continue;
        }
        switch (g.ruleb) {
        case 0: sum += s.incomeLast; break;
        case 1: sum += s.income; break;
        case 2: sum += s.incomeLast - s.upkeepLast; break;
        case 3: sum += s.income - s.upkeepTotal; break;
        case 4: sum += s.visitors; break;
        }
      }
      return static_cast<int>(std::llround(sum));
    }
    // The tanks by their walls (zoo.exe 0x48a0e3 rule 16: the habitats that are
    // tanks, their wall height +0x184 - steps, 5 a new tank's): ruleb 0 those
    // arga high or less, 1 arga or more, 2 exactly arga
    if (g.rulea == 16 && g.ruleb >= 0 && g.ruleb <= 2) {
      int n = 0;
      for (const Fences::Exhibit &ex : g_world->getFences().exhibits())
        if (ex.tank)
          n += g.ruleb == 0 ? ex.wallSub <= g.arga : g.ruleb == 1 ? ex.wallSub >= g.arga : ex.wallSub == g.arga;
      return n;
    }
    if (g.rulea == 99) // (arga itself)
      return g.arga;
    return 0;
  };
  h.measures = [](const Goals::Goal &g) {
    if (g.rulea == 1)
      return (g.ruleb >= 0 && g.ruleb <= 7) || g.ruleb == 11;
    return g.rulea == 9 || g.rulea == 10 || g.rulea == 11 || (g.rulea == 16 && g.ruleb >= 0 && g.ruleb <= 2) ||
           (g.rulea == 2 && g.ruleb >= 0 && g.ruleb <= 3) ||
           (g.rulea == 4 && ((g.ruleb >= 0 && g.ruleb <= 15) || g.ruleb == 19)) ||
           ((g.rulea == 5 || g.rulea == 6) && g.ruleb >= 0 && g.ruleb <= 6) ||
           (g.rulea == 7 && g.ruleb >= 0 && g.ruleb <= 2) ||
           (g.rulea == 3 && g.ruleb >= 0 && g.ruleb <= 2) ||
           (g.rulea == 0 && g.arga == 2 && (g.ruleb == 14 || g.ruleb == 15)) || (g.rulea == 13 && g.ruleb == 0) ||
           (g.rulea == 15 && g.ruleb >= 0 && g.ruleb <= 4) || g.rulea == 99;
  };
  h.objective = [](const std::string &text) {
    if (g_hud)
      g_hud->setObjective(text);
  };
  h.popup = [](const Goals::Hooks::Popup &popup) {
    if (!g_hud)
      return;
    UiGameScreen::ScenarioPopup spec;
    spec.layout = popup.layout;
    spec.background = popup.background;
    spec.icon = popup.icon;
    spec.image1 = popup.image1;
    spec.image2 = popup.image2;
    spec.text = popup.text;
    g_hud->popup(spec);
    // (pause=1: the game waits until it's been read)
    if (popup.pause && g_world && !g_world->isPaused()) {
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
  h.listIndex = [](int element) { return g_hud ? g_hud->listSelection(element) : -1; };
  h.listText = [](int element) { return g_hud ? g_hud->listText(element) : std::string(); };
  h.visible = [](int element) {
    UiElement *e = g_hud ? g_hud->getElementById(element) : nullptr;
    return e && !e->isHidden();
  };
  // (the tutorial presses the button itself: a click in its middle, whose
  // action the game then does)
  h.click = [](int element) {
    if (!g_hud)
      return;
    const UiAction action = g_hud->clickElement(element);
    if (action != UiAction::NONE)
      g_queuedActions.push_back(static_cast<int>(action));
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
  // (the game manager's month + year * 12, both from 0, as the rules')
  h.months = []() { return g_sim.month() + (g_sim.year() - 1) * 12; };
  h.available = [](int kind, const std::vector<int> &ids, bool on) {
    bool changed = false;
    for (const CatalogItem &item : ItemCatalog::get().all()) {
      const bool hit = kind == 2 || (kind == 1 && item.members.count("animals")) ||
                       (kind == 0 && std::find(ids.begin(), ids.end(), item.nameId) != ids.end());
      if (hit)
        changed = ItemCatalog::get().setAvailable(item, on) || changed;
    }
    if (changed && g_hud)
      g_hud->refreshCatalog();
    return changed;
  };
  h.reward = [](int nameId) { keepReward(nameId); };
  // goalFire 5, an animal given (zoo.exe 0x5a17a8): the first type of that
  // cNameID is made as the subtype names (m, f or y) and put down on the best
  // tile found round the entrance - rings of 3 to 20 tiles (the diamonds of
  // tiles whose x and y distances add to the ring; each ring from its far x
  // end, a tile's two y sides in turn). A tile counts when it is the zoo's
  // own ground (0x40fb8d: inside the zoo wall; its habitat +0x2c: no exhibit),
  // dry, with nothing standing on it; it scores a point each for a flat
  // tile with no path (+0x81 is 0), for none of its four quarter slots held,
  // and for one empty or held by a cAvoidEdges scenery (class slot 0xb4);
  // the first of 3 ends the search, else the first best of the rings
  h.giveAnimal = [](int species, int subtype) {
    if (!g_world)
      return false;
    Animals &an = g_world->getAnimals();
    int type = -1;
    for (size_t i = 0; i < an.types().size() && type < 0; i++)
      if (an.types()[i].nameId == species)
        type = static_cast<int>(i);
    // (the entrance is found in a world's first frames: asked for here too)
    if (!g_world->getGuests().hasEntrance())
      SaveGame::locateEntrance(*g_world);
    int ex = 0, ey = 0;
    if (type < 0 || !g_world->getGuests().entrance(ex, ey))
      return false;
    const WorldMap &map = g_world->getMap();
    const Fences &fn = g_world->getFences();
    WorldIndex &index = g_world->getIndex();
    auto standing = [&](int x, int y) {
      auto on = [x, y](float ux, float uy) { return static_cast<int>(std::floor(ux)) == x && static_cast<int>(std::floor(uy)) == y; };
      for (const PlacedObjects::Object &o : g_world->getObjects().objects())
        if (!o.fence && on(o.x, o.y))
          return true;
      for (const Animals::Member &m : an.members())
        if (on(m.x, m.y))
          return true;
      for (const Staff::Member &m : g_world->getStaff().members())
        if (on(m.x, m.y))
          return true;
      for (const Guests::Guest &g : g_world->getGuests().guests())
        if (!g.hidden && on(g.x, g.y))
          return true;
      return false;
    };
    int bestX = -1, bestY = -1, bestScore = INT_MIN;
    for (int ring = 3; ring <= 20 && bestScore < 3; ring++)
      for (int dx = -ring; dx <= ring && bestScore < 3; dx++)
        for (int side = 0; side < 2 && bestScore < 3; side++) {
          const int dy = side == 0 ? -(ring - std::abs(dx)) : ring - std::abs(dx);
          const int x = ex + dx, y = ey + dy;
          const MapTile *tile = map.getTile(x, y);
          if (!tile || x < 0 || y < 0 || x >= map.getWidth() || y >= map.getHeight() || !fn.insideZoo(x, y) ||
              fn.exhibitAt(x, y) >= 0 || tile->terrainType == 9 || tile->terrainType == 10 || standing(x, y))
            continue;
          const bool flat = tile->cornerHeight[0] == tile->cornerHeight[1] && tile->cornerHeight[1] == tile->cornerHeight[2] &&
                            tile->cornerHeight[2] == tile->cornerHeight[3];
          const int mask = index.quarterMask(x, y);
          const int score = (flat && !map.isPath(x, y) ? 1 : 0) + (mask == 0 ? 1 : 0) +
                            (mask != 15 || index.quarterAvoids(x, y) ? 1 : 0);
          if (score > bestScore) {
            bestScore = score;
            bestX = x;
            bestY = y;
          }
        }
    if (bestScore == INT_MIN)
      return false;
    // (put down as the original's world slot 0x18 adds it: no fit check)
    const int id = an.adopt(type, subtype == 1, bestX + 0.5f, bestY + 0.5f, map, fn, true);
    if (id < 0)
      return false;
    if (subtype == 2)
      if (Animals::Member *m = an.member(id)) {
        m->baby = true;
        m->grow = 0;
      }
    an.box(id); // (it comes in its crate: 0x505941)
    return true;
  };
  // goalFire 8, animals removed (zoo.exe 0x61c27d)
  h.removeAnimals = [](int kind, int id) {
    if (!g_world)
      return;
    Animals &an = g_world->getAnimals();
    std::vector<int> gone;
    for (const Animals::Member &m : an.members()) {
      const Animals::Type &t = an.types()[m.type];
      if ((kind == 0 || kind == 3) ? t.nameId == id : kind == 1 ? t.family == id : kind == 2 && t.genus == id)
        gone.push_back(m.id);
    }
    for (int member : gone)
      an.remove(member);
  };
  // goalFire 9, Sell Animal in Animal Information (zoo.exe 0x592fe3: the
  // button, element 0xc27)
  h.sellAnimals = [](bool allowed) {
    if (UiElement *e = g_hud ? g_hud->getElementById(3111) : nullptr)
      e->setDisabled(!allowed);
  };
  // goalFire 10, all research done (zoo.exe 0x47e657: each program's effect
  // done and, finishing, its progress filled; ours: a program done already
  // isn't applied twice)
  h.researchAll = [](bool finish) {
    for (ResearchBranch &b : Research::get().branches())
      for (ResearchCategory &c : b.categories)
        for (ResearchProgram &p : c.programs) {
          if (!p.done)
            applyResearch(p);
          if (finish) {
            p.done = true;
            p.progress = p.cost;
          }
        }
    if (g_hud)
      g_hud->refreshCatalog();
  };
  h.messageAbout = [rm](int id, int nameId) {
    if (!g_hud)
      return;
    std::string m = rm->getString(id);
    const CatalogItem *item = ItemCatalog::get().findName(nameId);
    if (size_t at = m.find("%s"); at != std::string::npos)
      m.replace(at, 2, item ? item->name : std::string());
    g_hud->postMessage(m);
  };
  h.ended = [](int outcome) {
    SDL_Log("[Goals] the scenario is over: %s", outcome == 1 ? "won" : "lost");
    g_scenarioOutcome = outcome;
  };
  g_goals.setHooks(h);
  // (every button clicked, told to the goals as 0x5b0f51 is)
  UiButton::clickObserver = [](int id) { g_goals.noteClick(id); };
  UiListBox::clickObserver = [](int id) { g_goals.noteClick(id); };
}

// ----------------------------------------------------------------------------
// Research finished: its effect (zoo.exe 0x58fbf0). 2 a characteristic of
// the target type (staff: keepers' cSlowRate; maintenance workers'
// cCleanTrashRadius, cFixFenceModifier; tour guides' cTourGuideBonus;
// animals: 0 cKeeperArrivesChange, 1 cFoodUnitValue, 2
// cReproductionChance, 3 cSickChance, 4 cTimeDeath (the living ones too),
// 5 cNumberAnimalsMax); 4 the same for every animal of an expansion
// (target < 100) or family; 5 chow's price. Changed by effectval3: 0 add,
// 1 set, 2 by a percentage of itself (rounded toward 0). 0 makes the target
// available (case 0x5b0f77: 0x58fd9f(target, 1) - every type of that cNameID,
// its slot 0x3c(1) - then the build panel shown afresh, 0x58fc10: a
// researched animal such as the gorilla is in no freeform unlock list, so
// research alone brings it). (1 animal houses' collections, 6 marine
// tricks: read where they're used; 7 a research discount: not done here)
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
  if (p.effect == 0) {
    if (const CatalogItem *item = ItemCatalog::get().findName(p.target))
      if (ItemCatalog::get().setAvailable(*item, true) && g_hud)
        g_hud->refreshCatalog();
    return;
  }
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
        t.speed = Original::unitTilesPerSecond(t.slowRate);
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
// what's done applied (costing nothing: done from the start), and each
// program once more for each time a rename cheat applied it without its
// being done (zoo.exe 0x607001; ours: kept while the zoo runs, as the
// original's save holds only a program's progress)
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
    st[i].speed = Original::unitTilesPerSecond(st[i].slowRate);
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
        for (int n = (p.done || p.cost <= 0 ? 1 : 0) + p.cheatApplied; n > 0; n--)
          applyResearch(p);
}

// The rename cheats' money and research (ours: their way out of the world)
static void setRenameHooks(ResourceManager *rm) {
  RenameCheats::Hooks h;
  h.rm = rm;
  h.research = [](const ResearchProgram &p) { applyResearch(p); };
  h.donation = [](double amount) { g_sim.earn(ZooSim::Donations, amount); };
  h.removalRefund = [](double amount) { g_sim.earn(ZooSim::Recycling, amount); };
  h.unlockScenarios = [] {
    for (int i = 0; g_scenarioManager && g_scenarioManager->getScenario(i); i++) {
      const ScenarioInfo *info = g_scenarioManager->getScenario(i);
      if (!info->unlocks.empty())
        Settings::setInt("scenario", info->unlocks.front(), 0);
    }
  };
  // "Xanadu", "Cretaceous Corral", "Kathmandu", "Super Croc" (typesSetAvailable
  // 0x591325 with 1): every type of the cNameID available now
  h.typeAvailable = [](int nameId) {
    if (const CatalogItem *item = ItemCatalog::get().findName(nameId))
      if (ItemCatalog::get().setAvailable(*item, true) && g_hud)
        g_hud->refreshCatalog();
  };
  RenameCheats::setHooks(h);
  // The name table (zoo.exe 0x514554, at a zoo's start) and its names used
  // once: emptied, then the zoo's own read in (cheatsLoad 0x594197 - ours:
  // the zoo's reader holds them, or a saved game's version 25)
  RenameCheats::loadNameTable(rm);
  RenameCheats::usedNames().clear();
  if (g_world)
    for (const std::string &name : g_world->mapReader().getUsedNames())
      RenameCheats::usedNames().insert(name);
}

// A map's goals, started as it is
static void startGoals(ResourceManager *rm, const std::string &mapPath, bool resume = false) {
  SaveGame::goals = &g_goals;
  setupGoals(rm);
  Research::get().restart();
  resetResearchEffects();
  g_rateNow = true; // (zoo.exe worldAfterLoad 0x4534e6: rated at once)
  setRenameHooks(rm);
  g_world->getStaff().foodPrice = [](const std::string &, double price) {
    auto it = g_foodPercent.find("");
    if (it == g_foodPercent.end())
      return price;
    // (the price as cents, changed as research said: 10000 = unchanged)
    long cents = static_cast<long>(price * 100);
    return static_cast<long>(cents * static_cast<double>(it->second) / 10000.0) * 0.01;
  };
  // (what the triggers made available starts afresh; the [start] triggers
  // fire as the goals load)
  ItemCatalog::get().clearAvailable();
  const bool scenario = mapPath.rfind("scenario/", 0) == 0;
  ItemCatalog::get().setMode(scenario, Features::allUnlocked);
  // zoo.exe 0x591426 (applyStartSettings's dated items, run as a zoo starts or
  // loads): Holiday Tree 7077, Jack-O'-Lantern 7078, Snowman 7079 and the
  // Yellow Brick Path 9111 start unavailable, in a scenario too; their day
  // (or lions, tigers and bears together) brings each in (Ambient). The
  // flyers' latches are cleared as well (0x63e256, 0x63e25c)
  for (int dated : {7077, 7078, 7079, 9111})
    if (const CatalogItem *item = ItemCatalog::get().findName(dated))
      ItemCatalog::get().setAvailable(*item, false);
  if (g_world) {
    Ambient &amb = g_world->getAmbient();
    amb.snowmanOn = amb.treeOn = amb.pumpkinOn = amb.yellowDone = false;
    amb.santaSent = amb.biplaneSent = false;
    Celebrations::reset();
    {
      // (the real date, GetLocalTime 0x412c22: 31 October of the PC's clock)
      const std::time_t now = std::time(nullptr);
      std::tm local{};
      localtime_s(&local, &now);
      RenameCheats::halloweenStart(*g_world, local.tm_mday, local.tm_mon + 1);
    }
    amb.setTypeAvailable = [](int nameId, bool on) {
      if (const CatalogItem *item = ItemCatalog::get().findName(nameId))
        if (ItemCatalog::get().setAvailable(*item, on) && g_hud)
          g_hud->refreshCatalog();
    };
  }
  // The scenario page (zooinfo4.lyt) lists this scenario's goals, and its
  // button is for a scenario only (greyed in a freeform zoo, as measured;
  // before the goals load: a tutorial's [start] triggers may disable it)
  if (g_hud)
    if (UiElement *button = g_hud->getElementById(4107))
      button->setDisabled(!scenario);
  // (Sell Animal allowed again: a scenario's [start] may grey it, trigger 9)
  if (g_hud)
    if (UiElement *sell = g_hud->getElementById(3111))
      sell->setDisabled(false);
  g_goals.load(rm, mapPath, resume);
  if (g_hud) {
    g_hud->setObjective("");
    g_hud->onObjectiveClick = [] { g_goals.reopenPopup(); };
    g_hud->scenarioPage = [] {
      UiGameScreen::ScenarioPage page;
      page.name = g_goals.scenarioName();
      page.story = g_goals.scenarioStory();
      for (const Goals::Line &line : g_goals.lines())
        page.lines.push_back({line.text, line.icon});
      return page;
    };
  }
  // (then the scenarios' rewards kept, each made available - after the
  // [start] triggers (applyStartSettings), as scenarioStart 0x4c998b calls
  // 0x4533bc: zoo.ini [user] unlockEntity0..unlockCount-1)
  for (int n = 0, count = Settings::getInt("user", "unlockCount", 0); n < count; n++)
    if (const CatalogItem *item = ItemCatalog::get().findName(Settings::getInt("user", "unlockEntity" + std::to_string(n), 0)))
      ItemCatalog::get().setAvailable(*item, true);
  if (g_hud)
    g_hud->refreshCatalog();
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
  // (zoo.exe saveZooDialog 0x4769ac: the dialog's file kept as lastfile)
  if (ok && given.empty())
    SaveGame::setLastFile(path);
  g_hud->showMessage(ok ? "Zoo saved." : (SaveGame::lastError().empty()?"The zoo couldn't be saved.":SaveGame::lastError()),
                     ok ? SDL_Color{255, 255, 255, 255} : SDL_Color{255, 40, 40, 255});
  return ok;
}

// Exit Game, Main Menu and Load Game ask first whether to save the zoo
// (zoo.exe gameopt1_ExitGame_click 0x501713 -> 0x501699, string 151;
// gameopt1_Startnew_click 0x501df6 -> 0x501d71, string 153;
// gameopt1_LoadGame_click 0x4cb9b4 -> 0x4cb8ed, string 152): unless Save
// Game (1502) is switched off (state 2), showDialog 0x4fff2c style 3 with
// the question picture. Yes: the Save dialog (saveZooDialog 0x4769ac), and
// on only once it saved; No: on at once; Cancel: stays. Load Game with Load
// Game itself (1501) switched off does nothing. "On" is the same action
// queued again, which doesn't ask. Gives true when the action stops here.
static std::string g_saveFirstFile; // (checks: the Yes answer's file, in place of the Save dialog)

// Save Game clicked: nothing while it's switched off; the Save dialog, and
// once it saved, Game Options closes (zoo.exe gameopt1_SaveGame_click
// 0x477041 -> 0x477004: saveZooDialog 0x4769ac, then uiFind 5 hidden) - and
// the Options window in its place. (checks: the file given)
static void saveGameClicked(const std::string &given = "") {
  UiElement *save = g_hud ? g_hud->getElementById(1502) : nullptr;
  if (save && save->isDisabled())
    return;
  if (gameSave(given) && g_hud) {
    if (g_hud->isPanelOpen(5))
      g_hud->toggleGameMenu();
    if (g_hud->optionsOpen())
      g_hud->options->close();
  }
}
static bool askSaveFirst(ResourceManager *rm, UiAction action) {
  if (!g_hud)
    return false;
  auto off = [](int id) {
    UiElement *e = g_hud->getElementById(id);
    return e && e->isDisabled();
  };
  if (action == UiAction::GAME_LOAD && off(1501))
    return true;
  if (off(1502))
    return false;
  const int text = action == UiAction::GAME_EXIT ? 151 : action == UiAction::GAME_LOAD ? 152 : 153;
  const int queued = static_cast<int>(action);
  g_hud->askYesNoCancel(
      rm->getString(text),
      [queued] {
        if (gameSave(g_saveFirstFile))
          g_queuedActions.push_back(queued);
      },
      [queued] { g_queuedActions.push_back(queued); });
  return true;
}

// Load Game: "Load a zoo..." (or the file given): its map started afresh
// with a new screen, then the save put back over it
static bool gameLoad(ResourceManager *rm, const std::string &given = "") {
  if (!g_world) return false;
  const std::string path = given.empty() ? SaveGame::askLoadPath() : given;
  if (path.empty()) return false;
  const Uint32 loadStarted = SDL_GetTicks();
  Uint32 loadStage = loadStarted;
  auto loadedStage = [&](const char *stage) {
    const Uint32 now = SDL_GetTicks();
    SDL_Log("[Save] load stage %s: %u ms (%u ms total)", stage,
            unsigned(now - loadStage), unsigned(now - loadStarted));
    loadStage = now;
  };
  SDL_Log("[Save] begin loading %s", path.c_str());
  // The menu worker may still be filling these lists. load() takes each
  // loader's mutex, so wait before copying or restarting research state.
  ItemCatalog::get().load(rm);
  Research::get().load(rm);
  loadedStage("catalogue and research ready");
  // Validate a separate world before replacing the current unsaved session.
  const auto researchBefore = Research::get().branches();
  const auto tickBefore = Research::get().tickClock;
  const auto zoomBefore = g_ZoomLevel;
  Goals *const previousGoals = SaveGame::goals;
  const bool original = SaveGame::isOriginal(path);
  const std::string mapPath = original ? path : SaveGame::mapOf(path);
  const bool scenario = mapPath.rfind("scenario:", 0) == 0;
  auto next = std::make_unique<World>(rm);
  const bool started = !mapPath.empty() && (scenario ? next->loadScenario(mapPath.substr(9))
                                                     : next->loadFreeform(mapPath));
  loadedStage("map ready");
  auto savedHud = std::make_unique<UiGameScreen>(rm, "ui/main.lyt", "ui/gamescrn.lyt");
  loadedStage("interface ready");
  ZooSim savedBooks; savedBooks.start(g_currentStartingCash, 37);
  Goals savedGoals;
  if (started) Research::get().restart();
  // The goals' script: the map's own; for a map that is an original saved
  // zoo, the script its scenario block names (gameMgrLoad 0x5948c6)
  std::string goalsPath = scenario ? mapPath.substr(9) : mapPath;
  const std::string scenarioScript = started ? next->mapReader().getScenarioName() : std::string();
  {
    std::string ext = goalsPath.size() >= 4 ? goalsPath.substr(goalsPath.size() - 4) : "";
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    if (ext != ".scn" && !scenarioScript.empty() && rm->hasResource(scenarioScript))
      goalsPath = scenarioScript;
  }
  savedGoals.load(rm, goalsPath);
  SaveGame::goals = &savedGoals;
  const bool ok = started && (original ? SaveGame::loadOriginal(*next, savedBooks, *savedHud)
                                      : SaveGame::load(path, *next, savedBooks, *savedHud));
  loadedStage("save decoded");
  SaveGame::goals = previousGoals;
  // An original zoo's goal states, in the original's order (zoo.exe 0x5950ab)
  if (ok && original && goalsPath == scenarioScript) {
    const auto &states = next->mapReader().getGoalStates();
    savedGoals.restoreOriginal(std::vector<int>(states.begin(), states.end()), next->mapReader().getVersion());
  }
  if (!ok) {
    Research::get().branches() = researchBefore;
    Research::get().tickClock = tickBefore;
    g_ZoomLevel = zoomBefore;
    std::string message = !started || SaveGame::lastError().empty()
        ? "That zoo couldn't be loaded. Your current zoo is unchanged." : SaveGame::lastError();
    if(!started&&!mapPath.empty()&&!rm->hasResource(mapPath)&&!std::filesystem::exists(mapPath))
      message="Missing map: "+std::filesystem::path(mapPath).filename().string()+". Enable its mod and try again.";
    SDL_Log("[Save] load rejected: %s: %s", path.c_str(), message.c_str());
    if (g_hud) g_hud->showMessage(message, SDL_Color{255, 40, 40, 255});
    else SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Zoo could not be loaded", message.c_str(), nullptr);
    return false;
  }
  const auto restoredResearch = Research::get().branches();
  const auto restoredTick = Research::get().tickClock;
  const auto restoredInfo = savedHud->zooInfo();
  std::vector<int> restoredStates;
  for (const auto &goal : savedGoals.all()) restoredStates.push_back(goal.state);
  const auto restoredAwards = savedGoals.awards;
  destroyHud();
  delete g_world;
  g_world = next.release();
  g_gameSpeed=1;g_speedClock.reset();g_resetSimulationFrame=true;
  delete g_nextHud;
  g_nextHud = savedHud.release();
  createHud(rm, g_world, g_currentStartingCash);
  g_currentMapPath = mapPath;
  startGoals(rm, goalsPath, true);
  g_sim = std::move(savedBooks);
  g_hud->setZooInfo(restoredInfo);
  if (goalsPath.size() >= 4 && SDL_strcasecmp(goalsPath.c_str() + goalsPath.size() - 4, ".scn") == 0)
    applyScenarioEconomy(rm, goalsPath);
  g_goals.restore(restoredStates);
  // (what the goals had made available, as the saved zoo kept it: over the
  // [resume] triggers, which leave out the [start] ones that unlock)
  if (SaveGame::availabilityKept) {
    ItemCatalog::get().restoreForced(SaveGame::availability);
    if (g_hud)
      g_hud->refreshCatalog();
  }
  g_goals.awards = restoredAwards;
  g_hud->setAwards(restoredAwards);
  Research::get().branches() = restoredResearch;
  Research::get().tickClock = restoredTick;
  g_ZoomLevel = g_world->getCamera().zoom;
  resetResearchEffects();
  updateHudClock(rm);
  loadedStage("zoo activated");
  SDL_Log("[Save] loaded %s", path.c_str());
  g_hud->showMessage("Zoo loaded.", SDL_Color{255, 255, 255, 255});
  return true;
}
// Apply switches between frames. The checkpoint is private runtime state,
// never the player's save. Required content is checked before rebuilding.
static bool applyModSelection(ResourceManager *rm, UiLayout *&layout, IniReader *&lytReader) {
  Mods::loadSelectionSettings();
  const auto previous = Mods::appliedFolders();
  const auto selected = Mods::list();
  std::string blocked;
  for (const auto &folder : previous) {
    bool enabled = false;
    for (const auto &mod : selected) if (mod.folder == folder) enabled = mod.enabled && Features::mods;
    if (!enabled) {
      const std::string why = Mods::offReason(folder);
      if (!why.empty()) { blocked = why; break; }
    }
  }
  if (!blocked.empty()) {
    Mods::restoreApplied();
    HdArt::refresh(); Shaders::refresh();
    UiOptions *options = g_hud ? g_hud->options : g_menuOptions;
    if (options) options->refreshModSwitches();
    if (g_hud) g_hud->showMessage(blocked, SDL_Color{255, 210, 90, 255});
    SDL_Log("[Mods] switch blocked: %s", blocked.c_str());
    return false;
  }

  ItemCatalog::get().load(rm); Research::get().load(rm); // wait for preloading
  const bool inGame = g_currentState == LayoutState::GAME_LOOP && g_hud;
  const bool paused = g_world && g_world->isPaused();
  UiOptions *options = inGame ? g_hud->options : g_menuOptions;
  const bool optionsOpen = options && options->isOpen();
  const int optionsPage = optionsOpen ? options->shownPage() : UiOptions::kModsPage;
  const int modStart = options ? options->modListStart() : 0;
  const LayoutState menuState = g_currentState;
  int menuSelection = 0;
  if (layout) if (auto *list = dynamic_cast<UiListBox *>(layout->getElementById(50002))) menuSelection = list->getSelectedIndex();
  const auto oldResearch = Research::get().branches();
  std::map<int, std::pair<std::string, std::string>> guestTypes;
  std::vector<size_t> counts;
  double cash = g_sim.cash();
  auto zooCounts = [] {
    return std::vector<size_t>{g_world->getAnimals().members().size(), g_world->getStaff().members().size(),
      g_world->getGuests().guests().size(), g_world->getObjects().objects().size(),
      g_world->getFences().pieces().size(), g_world->getFences().exhibits().size()};
  };
  std::filesystem::path checkpoint;
  if (inGame) {
    counts = zooCounts();
    for (const auto &guest : g_world->getGuests().guests()) {
      std::string type, favourite;
      if (guest.type >= 0 && guest.type < static_cast<int>(g_world->getGuests().types().size())) type = g_world->getGuests().types()[guest.type].key;
      if (guest.favourite >= 0 && guest.favourite < static_cast<int>(g_world->getAnimals().types().size())) favourite = g_world->getAnimals().types()[guest.favourite].file;
      guestTypes[guest.id] = {type, favourite};
    }
    std::error_code ec;
    auto dir = std::filesystem::path(Paths::dataDir()) / "mod-sessions";
    std::filesystem::create_directories(dir, ec);
    checkpoint = dir / ("runtime-" + std::to_string(SDL_GetPerformanceCounter()) + ".zt1save");
    if (ec || std::filesystem::exists(checkpoint) || !SaveGame::save(checkpoint.string(), *g_world, g_sim, *g_hud, g_currentMapPath)) {
      Mods::restoreApplied();
      g_hud->showMessage("The mod change could not preserve this zoo; your switches were restored.", SDL_Color{255, 100, 80, 255});
      SDL_Log("[Mods] live switch rolled back: checkpoint failed");
      return false;
    }
  }

  auto discard = [&] {
    destroyHud();
    delete g_nextHud; g_nextHud = nullptr;
    delete g_menuOptions; g_menuOptions = nullptr;
    delete layout; layout = nullptr;
    delete g_world; g_world = nullptr;
    Sound::get().reload();
    SpriteManager::get().clear();
    SpriteDatabase::get().clear();
    g_scenarioListBox = g_freeformListBox = nullptr;
    g_scenarioDescText = g_freeformDescText = nullptr;
    g_scenarioMap = g_freeformMap = nullptr;
  };
  auto rebuild = [&]() -> bool {
    rm->reloadMods();
    HdArt::refresh(); Shaders::refresh();
    ItemCatalog::get().load(rm, true);
    Research::get().load(rm, true);
    delete g_scenarioManager;
    g_scenarioManager = new ScenarioManager(rm);
    g_scenarioManager->loadScenarios(); g_scenarioManager->loadFreeformMaps();
    g_scenarioManager->getDatabase()->loadState(Paths::scenarioProgressFile());
    g_scenarioManager->syncWithDatabase(); g_scenarioManager->sortByDifficulty();
    g_world = new World(rm);
    g_menuOptions = new UiOptions(rm);
    if (inGame) {
      if (!gameLoad(rm, checkpoint.string())) return false;
      // Match research and visitor identities by file/key, not registry indices.
      for (auto &branch : Research::get().branches()) for (const auto &old : oldResearch) if (branch.file == old.file) {
        branch.fundingLevel = old.fundingLevel;
        for (auto &category : branch.categories) for (const auto &oldCategory : old.categories) if (category.file == oldCategory.file)
          for (auto &program : category.programs) for (const auto &oldProgram : oldCategory.programs) if (program.file == oldProgram.file) {
            program.progress = oldProgram.progress; program.done = oldProgram.done; program.cheatApplied = oldProgram.cheatApplied;
          }
      }
      resetResearchEffects();
      for (const auto &[id, identity] : guestTypes) if (auto *guest = const_cast<Guests::Guest *>(g_world->getGuests().guest(id))) {
        const auto &types = g_world->getGuests().types();
        for (size_t n = 0; n < types.size(); n++) if (types[n].key == identity.first) guest->type = static_cast<int>(n);
        guest->favourite = identity.second.empty() ? -1 : g_world->getAnimals().typeOfFile(identity.second);
      }
      setGamePaused(paused);
      if (zooCounts() != counts || std::abs(g_sim.cash() - cash) > 0.01) return false;
      Mods::markApplied();
      if (optionsOpen && g_hud->options) {
        g_hud->options->open(true, optionsPage); g_hud->options->setModListStart(modStart);
      }
    } else {
      Mods::markApplied();
      if (menuState == LayoutState::MOD_MENU) {
        layout = ModMenu::open(rm, getLayoutPath(rm, "scenario.lyt"));
        if (auto *list = dynamic_cast<UiListBox *>(layout->getElementById(50002))) list->setSelectedIndex(menuSelection);
        ModMenu::selectionChanged(layout, rm);
        g_currentState = LayoutState::MOD_MENU;
      } else {
        lytReader = rm->getIniReader(getLayoutPath(rm, "startup.lyt"));
        layout = new UiLayout(lytReader, rm); hookMainMenu(layout, rm);
        g_currentState = LayoutState::MAIN_MENU;
        if (optionsOpen) { g_menuOptions->open(false, optionsPage); g_menuOptions->setModListStart(modStart); }
      }
    }
    return true;
  };
  discard();
  bool ok = rebuild();
  if (!ok) {
    SDL_Log("[Mods] could not restore zoo: rolling back selection");
    discard(); Mods::restoreApplied();
    if (!rebuild()) {
      SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Mod change failed", ("The zoo checkpoint is kept at " + checkpoint.string()).c_str(), nullptr);
      destroyHud();
      lytReader = rm->getIniReader(getLayoutPath(rm, "startup.lyt"));
      layout = new UiLayout(lytReader, rm); hookMainMenu(layout, rm);
      g_currentState = LayoutState::MAIN_MENU;
      return false;
    }
  }
  Mods::markApplied();
  if (g_hud) g_hud->showMessage(ok ? "Mod switches applied." : "Mod change cancelled; previous switches and zoo restored.",
                              ok ? SDL_Color{255, 255, 255, 255} : SDL_Color{255, 210, 90, 255});
  if (!checkpoint.empty()) { std::error_code ec; std::filesystem::remove(checkpoint, ec); }
  SDL_Log("[Mods] live switch %s; zoo preserved, no process restart", ok ? "applied" : "rolled back");
  return ok;
}

// Extra view rotation on top of each map's start rotation
static int g_renderCheckExtraRotation = 0;

static bool saveTargetToBmp(SDL_Renderer *renderer, SDL_Texture *target,
                            const std::string &path) {
  int w = 0, h = 0;
  ZT_QueryTexture(target, nullptr, nullptr, &w, &h);
  SDL_SetRenderTarget(renderer, target);
  SDL_Surface *surface =
      SDL_CreateSurface(w, h, SDL_PIXELFORMAT_ARGB8888);
  bool ok = surface &&
            ZT_RenderReadPixels(renderer, nullptr, SDL_PIXELFORMAT_ARGB8888,
                                 surface->pixels, surface->pitch) == 0 &&
            SDL_SaveBMP(surface, path.c_str());
  if (surface)
    SDL_DestroySurface(surface);
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
        ZT_QueryTexture(preview, nullptr, nullptr, &w, &h);
        SDL_Texture *target = SDL_CreateTexture(
            renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_TARGET, w, h);
        SDL_SetRenderTarget(renderer, target);
        ZT_RenderCopy(renderer, preview, nullptr, nullptr);
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

int runShowChecks(World &,UiGameScreen &,ZooSim &,const std::string &,const std::string &,const std::function<void(const char*)> &);
int runEverythingZoo(World &,UiGameScreen &,ZooSim &,ResourceManager *,const std::string &,const std::string &);
int runShowAllChecks(World &,UiGameScreen &,ZooSim &,ResourceManager *,const std::string &);
int runFinanceChecks(World &,UiGameScreen &,ZooSim &,ResourceManager *,const std::string &);

// A scenario started as from the menu (Play Tutorial and Scenario Game,
// zoo.exe scenarioStart 0x4c998b): its map, its money (the .scn's [start]
// setcash - not the freeform spinner's), its economy file and its goals,
// whose [start] triggers= fire then
static bool startScenario(ResourceManager *rm, const ScenarioInfo &info) {
  const bool loaded = g_world->loadScenario(info.scenarioPath);
  // (a save names it: "scenario:" and its .scn)
  g_currentMapPath = "scenario:" + info.scenarioPath;
  int scenarioCash = CASH_START;
  if (IniReader *scn = rm->getIniReader(info.scenarioPath)) {
    scenarioCash = scn->getInt("start", "setcash", CASH_START);
    delete scn;
  }
  createHud(rm, g_world, scenarioCash);
  applyScenarioEconomy(rm, info.scenarioPath);
  startGoals(rm, info.scenarioPath);
  return loaded;
}

// A scenario over (zoo.exe scenarioComplete 0x48a408, once allGoalsMet
// 0x48a726 has the outcome: 1 won, 2 lost):
// - the best result kept in zoo.ini [scenario] under its unlocks= name (1
//   won, 2 lost, 3 none yet: kept only if better, 0x48a80a / 0x4cab6a) - a
//   scenario whose locks= names are all 1 or less is open (0x58eb28);
// - won: each of its [completion] unlock= kept for freeform (0x598cdc:
//   [user] unlockCount, unlockEntity<n>);
// - its message: tutorials 16104 won / 16105 lost "-Select YES to play the
//   next tutorial" while there is one (1a, 1, 2, Dinosaur Digs' if it's
//   there, Marine Mania's 1, 2, 3 - an expansion that isn't, skipped:
//   0x4ab32c), the last 16102 / 16103; any other 16100 "Congratulations!
//   You have won!" / 16101 "You have lost!"; in the popup (45200) with Yes
//   and No (0x4ff63c). No: a tutorial to the main menu (screen 0x1a), any
//   other to the scenario list (0x20); Yes: the next tutorial, or play on
static void scenarioComplete(ResourceManager *rm, int outcome) {
  if (!g_hud)
    return;
  const std::string scn = g_goals.script();
  const ScenarioInfo *info = nullptr;
  for (int i = 0; g_scenarioManager && g_scenarioManager->getScenario(i); i++)
    if (SDL_strcasecmp(g_scenarioManager->getScenario(i)->scenarioPath.c_str(), scn.c_str()) == 0)
      info = g_scenarioManager->getScenario(i);
  if (info && !info->unlocks.empty()) {
    const std::string token = info->unlocks.front();
    if (outcome < Settings::getInt("scenario", token, 3))
      Settings::setInt("scenario", token, outcome);
  }
  if (outcome == 1)
    if (IniReader *ini = rm->getIniReader(scn)) {
      for (const std::string &v : ini->getList("completion", "unlock"))
        keepReward(std::atoi(v.c_str()));
      delete ini;
    }
  auto is = [&](const char *path) { return SDL_strcasecmp(scn.c_str(), path) == 0; };
  auto there = [&](const char *path) { return rm->hasResource(path); };
  const char *dino = "scenario/scn19/scn19.scn", *marine = "scenario/scn27/scn27.scn";
  bool tutorial = false;
  std::string next;
  if (is("scenario/scn01a/scn01a.scn"))
    next = "scenario/scn01/scn01.scn";
  else if (is("scenario/scn01/scn01.scn"))
    next = "scenario/scn02/scn02.scn";
  else if (is("scenario/scn02/scn02.scn"))
    next = there(dino) ? dino : there(marine) ? marine : "";
  else if (is(dino))
    next = there(marine) ? marine : "";
  else if (is(marine))
    next = "scenario/scn28/scn28.scn";
  else if (is("scenario/scn28/scn28.scn"))
    next = "scenario/scn28b/scn28b.scn";
  tutorial = !next.empty() || is("scenario/scn02/scn02.scn") || is(dino) || is("scenario/scn28b/scn28b.scn");
  const int text = (outcome != 1 ? 1 : 0) + (!next.empty() ? 16104 : tutorial ? 16102 : 16100);
  if (g_world && !g_world->isPaused())
    setGamePaused(true);
  g_hud->popupYesNo(
      rm->getString(text),
      [next] {
        if (!next.empty())
          g_nextScenario = next;
        else if (g_world && g_world->isPaused())
          setGamePaused(false); // (play on)
      },
      [tutorial] {
        g_queuedActions.push_back(static_cast<int>(UiAction::GAME_MAIN_MENU));
        if (!tutorial)
          g_queuedActions.push_back(static_cast<int>(UiAction::STARTUP_PLAY_SCENARIO));
      });
}

// --panel-shots <outDir> [mapStem]: start a freeform game (default Death
// Mountain), open every toolbar panel and buy tab in turn and save each
// frame as <outDir>/<name>.bmp, then quit. For comparing with the original
// without driving the mouse.
static int runPanelShots(SDL_Renderer *renderer, ResourceManager *rm,
                         ScenarioManager *sm, World *world,
                         const std::string &outDir, const std::string &stem) {
  // As if from the freeform menu: the background preload has finished and
  // the in-game screen is ready. (The checks drive the original Game
  // Options panel - its Extras tab, its Load and Save - so they run with
  // the Options window off; the window is checked on its own, below)
  Features::optionsWindow = false;
  ItemCatalog::get().load(rm);
  Research::get().load(rm);
  prepareHud(rm);
  world->edgeScroll = false;
  // ZT_SCENARIO_CHECKS=1: every scenario started as from the menu, its
  // goals checked 40 times (a minute of game time) with each popup they
  // show read and closed - <out>/scenario-checks.txt
  if (std::getenv("ZT_SCENARIO_CHECKS")) {
    std::filesystem::create_directories(outDir);
    std::ofstream rep(outDir + "/scenario-checks.txt");
    int count = 0, bad = 0;
    for (int i = 0; const ScenarioInfo *info = sm->getScenario(i); i++) {
      bool loaded = startScenario(rm, *info);
      int popups = 0;
      for (int k = 0; k < 40 && g_hud; k++) {
        g_goals.update(1.5f);
        // (read and dismissed, as a player would: Enter)
        for (int tries = 0; g_hud->hasDialog() && tries < 20; tries++) {
          popups += tries == 0 ? 1 : 0;
          std::vector<Input> enterKey(1);
          enterKey[0].type = InputType::BUTTON;
          enterKey[0].event = InputEvent::KEY_DOWN;
          enterKey[0].key = SDLK_RETURN;
          g_hud->handleInputs(enterKey);
        }
      }
      int composites = 0;
      std::function<void(const std::vector<Goals::Node> &)> walk = [&](const std::vector<Goals::Node> &ns) {
        for (const Goals::Node &n : ns) {
          composites += n.kind != 0 ? 1 : 0;
          walk(n.children);
        }
      };
      walk(g_goals.tree());
      // The scenario page (zooinfo4.lyt, zoo.exe 0x4f004a), opened: the name
      // and story, then a line for the time limit and each goal shown (not
      // the hidden ones), their numbers and type names filled in
      std::string pageNote;
      if (g_hud && g_hud->scenarioPage && g_hud->scenarioPageLayout()) {
        g_hud->setPanelOpen(803, true);
        const UiGameScreen::ScenarioPage page = g_hud->scenarioPage();
        for (const auto &line : page.lines)
          rep << "    | " << line.first << std::endl;
        size_t visible = 0;
        for (const Goals::Goal &goal : g_goals.all())
          visible += goal.hidden || !goal.text ? 0 : 1;
        bool filled = true;
        for (const auto &line : page.lines)
          filled = filled && !line.first.empty() && line.first.find('%') == std::string::npos &&
                   line.first.find("String #") == std::string::npos && !line.second.empty();
        UiLayout *panel = g_hud->scenarioPageLayout();
        const UiListBox *list = dynamic_cast<UiListBox *>(panel->getElementById(4145));
        const UiText *story = dynamic_cast<UiText *>(panel->getElementById(4143));
        const bool pageOk = !page.name.empty() && !page.story.empty() && filled && list &&
                            list->getItemCount() == page.lines.size() && story && story->getText() == page.story &&
                            (page.lines.size() == visible || page.lines.size() == visible + 1);
        if (!pageOk)
          pageNote = " [page FAILED: name '" + page.name + "', " + std::to_string(page.story.size()) + " story chars, " +
                     std::to_string(page.lines.size()) + " lines for " + std::to_string(visible) + " goals]";
        else
          pageNote = " [page: " + std::to_string(page.lines.size()) + " lines]";
        g_hud->setPanelOpen(803, false);
        if (!pageOk)
          loaded = false;
      }
      const bool ok = loaded && g_hud && !g_goals.all().empty();
      count++;
      bad += ok ? 0 : 1;
      rep << (ok ? "PASS " : "FAIL ") << info->scenarioPath << pageNote << ": " << g_goals.all().size() << " goals, " << composites
          << " composites, limit " << g_goals.durationMonths << " months (state " << g_goals.durationState << "), "
          << popups << " popups, outcome " << g_goals.outcome << ", cash " << std::lround(g_sim.cash()) << "\n";
      rep.flush();
    }
    // The ending (scenarioComplete 0x48a408), the player's own zoo.ini
    // [scenario] and [user] unlocks put back afterwards
    {
      auto check = [&](const std::string &what, bool ok) {
        count++;
        bad += ok ? 0 : 1;
        rep << (ok ? "PASS " : "FAIL ") << what << "\n";
        rep.flush();
      };
      auto find = [&](const char *path) -> const ScenarioInfo * {
        for (int i = 0; const ScenarioInfo *s = sm->getScenario(i); i++)
          if (s->scenarioPath == path)
            return s;
        return nullptr;
      };
      const int keptBa = Settings::getInt("scenario", "ba", 3), keptAa = Settings::getInt("scenario", "aa", 3);
      const int keptCount = Settings::getInt("user", "unlockCount", 0);
      const ScenarioInfo *small = find("scenario/scn03/scn03.scn"), *tut1 = find("scenario/scn01a/scn01a.scn");
      if (small && tut1) {
        Settings::setInt("scenario", "ba", 3);
        startScenario(rm, *small);
        // Tutorial 1's reminder bar (objectve.lyt): the popup that asks for the
        // Game Options button (a persistentShortText of -1) puts the first goal
        // still to do on it, and a click on it brings that popup back
        // (objectve_Message_click 0x4a09f8 -> loadScenarioPopup 0x5a1b15)
        if (const ScenarioInfo *tutorial = find("scenario/scn01a/scn01a.scn")) {
          startScenario(rm, *tutorial);
          // (the step counter, rule 9, brings its popup by itself; the goal that
          // would put the bar away again reads the guests' happiness, which
          // the map's zoo decides: it stays quiet)
          for (Goals::Goal &goal : g_goals.mutableAll())
            if (goal.file == tutorial->scenarioPath && goal.trulea == 2 && goal.targa == 8)
              goal.trulea = 0;
          g_goals.evaluateAll();
          int seenPopups = 0;
          for (int tries = 0; g_hud && g_hud->hasDialog() && tries < 20; tries++) {
            seenPopups++;
            g_hud->answerDialog(true);
          }
          const std::string bar = g_hud ? g_hud->objectiveText() : "";
          const bool shown = !bar.empty() && bar == rm->getString(17732) && g_hud->isPanelOpen(45400);
          const bool closed = g_hud && !g_hud->hasDialog();
          if (g_hud)
            g_hud->clickObjective();
          const bool again = g_hud && g_hud->hasDialog() &&
                             g_hud->dialogMessage().find(langText("TUTORIAL_T1A_PROMPT2").substr(0, 20)) != std::string::npos;
          while (g_hud && g_hud->hasDialog())
            g_hud->answerDialog(true);
          check("Tutorial 1's reminder bar shows the first goal still to do (\"" + bar + "\"; " + std::to_string(seenPopups) +
                    " popups, " + std::to_string(shown) + std::to_string(closed) + std::to_string(again) +
                    ") and a click brings its popup back",
                shown && closed && again);
          startScenario(rm, *small);
          check("a scenario without one has no reminder bar", g_hud && g_hud->objectiveText().empty() && !g_hud->isPanelOpen(45400));
        }
        // The tutorials' steps (#100): rule 9 counts the goals in a state (the
        // scenario's own goal, its time limit, met at once, among them, so a
        // tutorial's first goal "1 goal in state 1" fires at the start, the
        // next "2" with it...), rule 10 waits for a click on a button - one
        // made after the goal first asked about it (0x4f052c: the first ask
        // registers the observer, the second enters the button) - and the
        // popups come out in order. Each tutorial is run from start to end
        // here: the buttons are clicked on the HUD itself; what needs the
        // player's building (rules 1, 2, 3, 5 and 6: a fence, a tiger, a
        // keeper, paths, trees) is met by hand, one goal at a time in the
        // goals' order, as the zoo would
        {
          // (a click on a HUD button, and what the game does for Pause and Play)
          auto press = [&](int id) {
            drawHud(renderer, rm); // (the buttons' places, as the player sees them)
            const UiAction a = g_hud->clickElement(id);
            if (a == UiAction::HUD_PAUSE || a == UiAction::HUD_PLAY)
              setGamePaused(a == UiAction::HUD_PAUSE);
            g_queuedActions.clear();
          };
          auto drain = [&](std::vector<std::string> *shown) {
            for (int tries = 0; g_hud && g_hud->hasDialog() && tries < 60; tries++) {
              if (shown)
                shown->push_back(g_hud->dialogMessage());
              g_hud->answerDialog(true);
            }
            resumeAfterPopup();
          };
          if (const ScenarioInfo *t1 = find("scenario/scn01a/scn01a.scn")) {
            // Clicks before the goal asked twice don't count
            startScenario(rm, *t1);
            drawHud(renderer, rm);
            g_goals.evaluateAll(); // (its first pass: the observer is registered)
            drain(nullptr);
            press(1004);           // (Game Options, shown by the goal show_options)
            g_goals.evaluateAll(); // (the second ask: the button is entered; the click was before it)
            drain(nullptr);
            auto stateOf = [&](const char *name) {
              for (const Goals::Goal &goal : g_goals.all())
                if (goal.name == name && goal.file == t1->scenarioPath)
                  return goal.state;
              return -1;
            };
            const bool lost = stateOf("GAME_OPTIONS") == 0;
            press(1004);
            g_goals.evaluateAll();
            const bool found = stateOf("GAME_OPTIONS") == 1;
            drain(nullptr);
            check("rule 10: a click before the goal's second look is lost, one after it counts (once)", lost && found);
          }
          struct Run {
            const char *path;
            const char *label;
            int popups;
          };
          for (const Run &run : {Run{"scenario/scn01a/scn01a.scn", "Tutorial 1", 8}, Run{"scenario/scn01/scn01.scn", "Tutorial 2", 13},
                                 Run{"scenario/scn02/scn02.scn", "Tutorial 3", 10},
                                 Run{"scenario/scn19/scn19.scn", "Dinosaur Digs Tutorial", 11},
                                 Run{"scenario/scn27/scn27.scn", "Marine Mania Tutorial 1", 12},
                                 Run{"scenario/scn28/scn28.scn", "Marine Mania Tutorial 2", 10},
                                 Run{"scenario/scn28b/scn28b.scn", "Marine Mania Tutorial 3", 6}}) {
            const ScenarioInfo *t = find(run.path);
            if (!t)
              continue;
            startScenario(rm, *t);
            // (the guests' happiness in the map's zoo, and goals whose element isn't there, are the
            // player's to do)
            std::vector<std::string> shown, byHand;
            std::map<std::string, int> tries; // (rounds a button has been waited for or pressed)
            int clicked = 0, rounds = 0;
            for (; rounds < 400; rounds++) {
              drawHud(renderer, rm); // (the buttons the tutorial presses are where the player sees them)
              g_goals.evaluateAll();
              drain(&shown);
              bool acted = false;
              for (Goals::Goal &goal : g_goals.mutableAll()) {
                if (goal.file != t->scenarioPath || goal.state != 0 || goal.rulea == 9)
                  continue;
                if (goal.rulea == 10 && goal.ruleb != 0) {
                  UiElement *e = g_hud->getElementById(goal.arga);
                  // (a button the check can't reach - in a panel it doesn't open, or that its
                  // click doesn't get to - is told as clicked by hand after a few rounds)
                  if (++tries[goal.name] > 6) {
                    g_goals.noteClick(goal.arga);
                    byHand.push_back("click " + std::to_string(goal.arga));
                    tries[goal.name] = -1000;
                    acted = true;
                    break;
                  }
                  // (Undo is greyed until the player has put something down: that is theirs to do)
                  if (e && goal.arga == 1075 && !e->isHidden() && e->isDisabled()) {
                    e->setDisabled(false);
                    byHand.push_back("something to undo");
                  }
                  if (e && !e->isHidden() && !e->isDisabled()) {
                    press(goal.arga);
                    clicked++;
                    acted = true;
                    break;
                  }
                  continue;
                }
                if (goal.rulea == 99 || (goal.type != 0 && goal.type != 1 && goal.type != 2))
                  continue;
                byHand.push_back(goal.name);
                goal.arga = goal.type == 1 ? goal.value - 1 : goal.value;
                goal.rulea = 99;
                acted = true;
                break;
              }
              if (!acted && !g_hud->hasDialog() && rounds > 3) {
                bool open = false;
                for (const Goals::Goal &goal : g_goals.all())
                  open = open || (goal.file == t->scenarioPath && goal.state == 0);
                if (!open)
                  break;
              }
            }
            int met = 0, total = 0;
            std::string waiting;
            for (const Goals::Goal &goal : g_goals.all())
              if (goal.file == t->scenarioPath) {
                total++;
                met += goal.state == 1 ? 1 : 0;
                if (goal.state != 1)
                  waiting += (waiting.empty() ? "" : ", ") + goal.name;
              }
            const std::string first = shown.empty() ? "" : shown.front().substr(0, 30);
            std::string order;
            for (const std::string &s : shown)
              {
                std::string t = s.substr(0, 60);
                std::replace_if(t.begin(), t.end(), [](char c) { return c == 10 || c == 13; }, 32);
                order += (order.empty() ? "" : " | ") + t;
              }
            rep << "  " << run.label << " popups: " << order << "\n  by hand: ";
            for (const std::string &s : byHand)
              rep << s << " ";
            rep << "\n";
            check(std::string(run.label) + " runs to its end: " + std::to_string(met) + " of " + std::to_string(total) +
                      " goals met (waiting: " + waiting + "), " + std::to_string(shown.size()) + " popups (" +
                      std::to_string(run.popups) + "), " + std::to_string(clicked) + " button clicks, " +
                      std::to_string(byHand.size()) + " goals by hand, " + std::to_string(rounds) + " rounds; first \"" + first +
                      "...\"; the reminder bar " + (g_hud->objectiveText().empty() ? "put away" : "\"" + g_hud->objectiveText() + "\""),
                  met == total && (run.popups < 0 || static_cast<int>(shown.size()) == run.popups) && g_hud->objectiveText().empty());
          }
          // Rule 11 (a list picked, 0x61bb06) with a list made up (the first ask registers, the
          // second enters it, a click then counts once): ruleb 0 its row against value, else the
          // row's text against the string argb; rule 16 (0x48a0e3) counts a map's tanks by wall height
          {
            const Goals::Hooks saved = g_goals.getHooks();
            Goals::Hooks fake = saved;
            int row = 0;
            std::string text = "Zoo Tycoon";
            fake.listIndex = [&](int) { return row; };
            fake.listText = [&](int) { return text; };
            g_goals.setHooks(fake);
            Goals::Goal byRow, byText;
            byRow.rulea = 11, byRow.ruleb = 0, byRow.arga = 22900, byRow.value = 2, byRow.type = 2; // (scn19's SORT_DINO)
            byText.rulea = 11, byText.ruleb = 1, byText.arga = 22900, byText.argb = 22903, byText.value = 1; // (scn27's FILTER_MARINE)
            const int firstAsk = g_goals.stateNow(byRow), secondAsk = g_goals.stateNow(byRow); // (nothing clicked)
            g_goals.noteClick(22900);
            row = 1;
            const int wrongRow = g_goals.stateNow(byRow); // (clicked, row 1: not 2; the click is used up)
            g_goals.noteClick(22900);
            row = 2;
            const int rightRow = g_goals.stateNow(byRow);
            const int usedUp = g_goals.stateNow(byRow); // (no new click: no)
            g_goals.noteClick(22900);
            const int wrongText = g_goals.stateNow(byText);
            text = rm->getString(22903);
            g_goals.noteClick(22900);
            const int rightText = g_goals.stateNow(byText);
            g_goals.setHooks(saved);
            check("rule 11: a list pick counts after a click, once - by its row (" + std::to_string(rightRow) + " at 2, " +
                      std::to_string(wrongRow) + " at 1) and by its text (\"" + rm->getString(22903) + "\": " +
                      std::to_string(rightText) + ", another's " + std::to_string(wrongText) + ")",
                  firstAsk == 0 && secondAsk == 0 && wrongRow == 0 && rightRow == 1 && usedUp == 0 && wrongText == 0 && rightText == 1);
          }
          if (const ScenarioInfo *mm = find("scenario/scn27/scn27.scn")) {
            startScenario(rm, *mm);
            int tanks = 0, five = 0, atLeastSix = 0, atMostFive = 0;
            for (const Fences::Exhibit &ex : g_world->getFences().exhibits())
              if (ex.tank) {
                tanks++;
                five += ex.wallSub == 5;
                atLeastSix += ex.wallSub >= 6;
                atMostFive += ex.wallSub <= 5;
              }
            Goals::Goal g;
            g.rulea = 16, g.arga = 5, g.value = 1, g.ruleb = 2;
            const int equalsFive = g_goals.getHooks().measure(g);
            g.ruleb = 1;
            g.arga = 6;
            const int sixOrMore = g_goals.getHooks().measure(g);
            g.ruleb = 0;
            g.arga = 5;
            const int fiveOrLess = g_goals.getHooks().measure(g);
            check("rule 16: the map's " + std::to_string(tanks) + " tanks by wall height (" + std::to_string(equalsFive) + " exactly 5, " +
                      std::to_string(sixOrMore) + " 6 or more, " + std::to_string(fiveOrLess) + " 5 or less)",
                  equalsFive == five && sixOrMore == atLeastSix && fiveOrLess == atMostFive);
          }
          startScenario(rm, *small);
          drain(nullptr);
        }
        // The scenario page's icons follow the goals' states (0x4f048c: the
        // [desc] iconp, iconc, iconf, then iconc again), the time limit's
        // line first with its own clock (0x4f0488)
        {
          std::vector<std::string> icons;
          for (int state : {0, 1, 2, 3}) {
            for (Goals::Goal &goal : g_goals.mutableAll())
              if (goal.name == "a" && goal.file == small->scenarioPath)
                goal.state = state;
            const auto lines = g_goals.lines();
            icons.push_back(lines.size() > 1 ? lines[1].icon : "");
          }
          for (Goals::Goal &goal : g_goals.mutableAll())
            goal.state = 0;
          UiElement *button = g_hud ? g_hud->getElementById(4107) : nullptr;
          check("the Scenario information button is on in a scenario", button && !button->isDisabled());
          const auto lines = g_goals.lines();
          check("scenario page: the time limit shows its clock, each goal the icon of its state (pending, met, failed, "
                "met again)",
                !lines.empty() && lines[0].icon == "ui/scenario/clock/clock" && icons[0] == "ui/scenario/iconp/iconp" &&
                    icons[1] == "ui/scenario/iconc/iconc" && icons[2] == "ui/scenario/iconf/iconf" && icons[3] == icons[1]);
        }
        // What it can buy (goalFire 4: 0x5a0fc2): [start] hideall (4/7)
        // leaves no animals; goal c (4/0, the giraffe 5033) gives one
        const size_t animalsAtStart = ItemCatalog::get().inCategory("animals").size();
        const size_t treesAtStart = ItemCatalog::get().inCategory("foliage").size();
        for (Goals::Goal &goal : g_goals.mutableAll())
          if (goal.name == "c" && goal.file == small->scenarioPath) {
            goal.rulea = 99; // (met: arga 1 against value 1)
            goal.arga = 1;
          }
        g_goals.evaluateAll();
        const auto animalsNow = ItemCatalog::get().inCategory("animals");
        std::set<int> names;
        for (const CatalogItem *a : animalsNow)
          names.insert(a->nameId);
        check("Small Zoo: no animals at the start (" + std::to_string(animalsAtStart) + "), foliage still (" +
                  std::to_string(treesAtStart) + "); the first check (its map has an exhibit: goals c, d, e) "
                  "makes the giraffe, moose and camel available (" + std::to_string(animalsNow.size()) + ")",
              animalsAtStart == 0 && treesAtStart > 0 && names == std::set<int>{5033, 5029, 5034});
        while (g_hud->hasDialog())
          g_hud->answerDialog(true);
        g_queuedActions.clear();
        scenarioComplete(rm, 1);
        check("Small Zoo won: \"" + g_hud->dialogMessage().substr(0, 32) + "...\" (16100), [scenario] ba = " +
                  std::to_string(Settings::getInt("scenario", "ba", 3)),
              g_hud->hasDialog() && g_hud->dialogMessage() == rm->getString(16100) && Settings::getInt("scenario", "ba", 3) == 1);
        g_hud->answerDialog(false);
        check("No: to the scenario list (by the main menu)",
              g_queuedActions == std::vector<int>{static_cast<int>(UiAction::GAME_MAIN_MENU),
                                                  static_cast<int>(UiAction::STARTUP_PLAY_SCENARIO)});
        g_queuedActions.clear();
        scenarioComplete(rm, 2);
        check("lost after a win: 16101, the win kept (ba " + std::to_string(Settings::getInt("scenario", "ba", 3)) + ")",
              g_hud->dialogMessage() == rm->getString(16101) && Settings::getInt("scenario", "ba", 3) == 1);
        g_hud->answerDialog(true);
        check("Yes: play on (nothing queued)", g_queuedActions.empty() && g_nextScenario.empty());
        sm->syncWithDatabase();
        const ScenarioInfo *s3 = find("scenario/scn03/scn03.scn"), *burk = find("scenario/scn07/scn07.scn");
        check("the list: Small Zoo done (" + s3->iconPath + "), Burkitsville still locked (needs bb, bc too)",
              s3->iconPath == "ui/scenario/iconc/iconc" && burk && burk->isLocked);
        // The Tutorial & Scenario screen's Play button: greyed for a scenario
        // that's locked, enabled for one that's open (zoo.exe
        // scenario_ScenarioList_click 0x58e9bb)
        {
          UiLayout *menu = new UiLayout(rm->getIniReader(getLayoutPath(rm, "scenario.lyt")), rm);
          populateScenarioList(menu, sm);
          UiElement *play = menu->getElementById(50008);
          auto choose = [&](const ScenarioInfo *want) {
            for (int i = 0; const ScenarioInfo *s = sm->getScenario(i); i++)
              if (s == want && g_scenarioListBox) {
                g_scenarioListBox->setSelectedIndex(i);
                updateScenarioDetails(menu, sm, rm);
                return true;
              }
            return false;
          };
          const bool lockedGrey = burk && choose(burk) && play && play->isDisabled();
          const bool openLit = choose(s3) && play && !play->isDisabled();
          check("the Play button: greyed for a locked scenario, lit for an open one", lockedGrey && openLit);
          // A locked scenario tells its own story ([desc] lockedstory, the
          // lang0.dll TEXT of that name, zoo.exe 0x600085), not ours
          {
            std::string want;
            if (burk)
              if (IniReader *ini = rm->getIniReader(burk->scenarioPath)) {
                std::string key = ini->get("desc", "lockedstory");
                delete ini;
                while (!key.empty() && std::isspace(static_cast<unsigned char>(key.back())))
                  key.pop_back();
                want = langText(key);
              }
            const bool shown = burk && choose(burk) && g_scenarioDescText && !want.empty() &&
                               g_scenarioDescText->getText() == want;
            check("a locked scenario shows its own lockedstory text (\"" + want.substr(0, 40) + "...\")", shown);
          }
          g_scenarioListBox = nullptr;
          g_scenarioDescText = nullptr;
          g_scenarioMap = nullptr;
          delete menu;
        }
        startScenario(rm, *tut1);
        scenarioComplete(rm, 2);
        check("tutorial 1 lost: 16105 (Yes plays the next)", g_hud->dialogMessage() == rm->getString(16105));
        g_hud->answerDialog(true);
        check("Yes: tutorial 2 next (" + g_nextScenario + ")", g_nextScenario == "scenario/scn01/scn01.scn");
        g_nextScenario.clear();
        check("a loss keeps 2 ([scenario] aa " + std::to_string(Settings::getInt("scenario", "aa", 3)) + ")",
              Settings::getInt("scenario", "aa", 3) == 2);
        startScenario(rm, *tut1);
        scenarioComplete(rm, 2);
        g_hud->answerDialog(false);
        check("No after a tutorial: the main menu only",
              g_queuedActions == std::vector<int>{static_cast<int>(UiAction::GAME_MAIN_MENU)});
        g_queuedActions.clear();
        // A reward (Dinosaur Digs' scenario 26: [completion] unlock=5332)
        // kept and buyable in a freeform zoo started after
        if (const ScenarioInfo *s26 = find("scenario/scn26/scn26.scn")) {
          const std::string token = s26->unlocks.empty() ? std::string() : s26->unlocks.front();
          const int keptToken = token.empty() ? 3 : Settings::getInt("scenario", token, 3);
          startScenario(rm, *s26);
          scenarioComplete(rm, 1);
          g_hud->answerDialog(true);
          const CatalogItem *item = ItemCatalog::get().findName(5332);
          const std::string keepMap = g_currentMapPath;
          g_currentMapPath = "freeform/deathmtn.scn";
          startGoals(rm, g_currentMapPath);
          check(std::string("scenario 26's reward 5332 (") + (item ? item->name : "?") + ") kept and buyable in freeform after",
                item && ItemCatalog::get().isAvailable(*item));
          g_currentMapPath = keepMap;
          if (!token.empty())
            Settings::setInt("scenario", token, keptToken);
        }
        // Goal rules and triggers the scenarios use (zoo.exe 0x41d665
        // measures, 0x4fc783 fires): an animal given by trigger 5, counted
        // by rules 2 and 4, taken away by trigger 8; Sell Animal greyed by
        // 9; every program researched by 10
        {
          auto measure = [&](int a, int b, int arga, int argb = 0) {
            Goals::Goal t;
            t.rulea = a;
            t.ruleb = b;
            t.arga = arga;
            t.argb = argb;
            return g_goals.getHooks().measure ? g_goals.getHooks().measure(t) : -1;
          };
          auto fireNamed = [&](const std::string &name) {
            for (Goals::Goal &goal : g_goals.mutableAll())
              if (goal.trulea == 5 && goal.name != name && goal.state == 0) {
                goal.rulea = 99; // (the scenario's other gifts held back: never met)
                goal.arga = 0;
                goal.value = 1;
                goal.type = 0;
              }
            for (Goals::Goal &goal : g_goals.mutableAll())
              if (goal.name == name) {
                goal.rulea = 99; // (met at once: arga 1 against value 1)
                goal.arga = 1;
                goal.value = 1;
                goal.type = 0;
              }
            g_goals.evaluateAll();
            while (g_hud->hasDialog())
              g_hud->answerDialog(true);
          };
          if (const ScenarioInfo *s04 = find("scenario/scn04/scn04.scn")) {
            startScenario(rm, *s04);
            Animals &an = g_world->getAnimals();
            const int species = 5004; // (the zebra: scenario 4 goals h, a male, and i, a female)
            const int before = static_cast<int>(an.members().size());
            const int sp0 = measure(2, 0, species), m0 = measure(2, 2, species, 0), f0 = measure(2, 2, species, 1);
            const int either0 = measure(2, 3, species, 1), kind0 = measure(4, 2, species);
            int bigType = -1;
            for (size_t i = 0; i < an.types().size(); i++)
              if (an.types()[i].nameId == species)
                bigType = static_cast<int>(i);
            const int expansion = bigType >= 0 ? an.types()[bigType].expansionId : 0;
            const int exp0 = measure(4, 19, expansion);
            int ex = 0, ey = 0;
            if (!g_world->getGuests().hasEntrance())
              SaveGame::locateEntrance(*g_world);
            g_world->getGuests().entrance(ex, ey);
            // The tile the original takes (0x5a17a8): the nearest ring, 3 to 20,
            // that has a tile scoring 3 (the zoo's own flat ground, no path, dry,
            // nothing on it, no quarter held) - found here from the map
            int wantRing = -1;
            {
              const WorldMap &wm = g_world->getMap();
              for (int ring = 3; ring <= 20 && wantRing < 0; ring++)
                for (int dx = -ring; dx <= ring && wantRing < 0; dx++)
                  for (int side = 0; side < 2 && wantRing < 0; side++) {
                    const int x = ex + dx, y = ey + (side == 0 ? -(ring - std::abs(dx)) : ring - std::abs(dx));
                    const MapTile *t = wm.getTile(x, y);
                    if (!t || !g_world->getFences().insideZoo(x, y) || g_world->getFences().exhibitAt(x, y) >= 0 ||
                        t->terrainType == 9 || t->terrainType == 10 || wm.isPath(x, y) ||
                        g_world->getIndex().quarterMask(x, y) != 0 ||
                        !(t->cornerHeight[0] == t->cornerHeight[1] && t->cornerHeight[1] == t->cornerHeight[2] &&
                          t->cornerHeight[2] == t->cornerHeight[3]))
                      continue;
                    bool taken = false;
                    for (const Animals::Member &m : an.members())
                      taken = taken || (static_cast<int>(m.x) == x && static_cast<int>(m.y) == y);
                    for (const PlacedObjects::Object &o : g_world->getObjects().objects())
                      taken = taken || (!o.fence && static_cast<int>(o.x) == x && static_cast<int>(o.y) == y);
                    for (const Staff::Member &m : g_world->getStaff().members())
                      taken = taken || (static_cast<int>(m.x) == x && static_cast<int>(m.y) == y);
                    for (const Guests::Guest &gu : g_world->getGuests().guests())
                      taken = taken || (!gu.hidden && static_cast<int>(gu.x) == x && static_cast<int>(gu.y) == y);
                    if (!taken)
                      wantRing = ring;
                  }
            }
            fireNamed("h");
            int gotRing = -1;
            if (!an.members().empty()) {
              const Animals::Member &first = an.members().back();
              gotRing = std::abs(static_cast<int>(first.x) - ex) + std::abs(static_cast<int>(first.y) - ey);
            }
            fireNamed("i");
            bool placed = static_cast<int>(an.members().size()) == before + 2 && measure(2, 0, species) == sp0 + 2,
                 near = true, kinds = false;
            for (const Animals::Member &m : an.members()) {
              if (an.types()[m.type].nameId != species)
                continue;
              const int d = std::abs(static_cast<int>(m.x) - ex) + std::abs(static_cast<int>(m.y) - ey);
              near = near && d >= 3 && d <= 20 && m.boxed;
            }
            kinds = measure(2, 0, species) == sp0 + 2 && measure(2, 2, species, 0) == m0 + 1 &&
                    measure(2, 2, species, 1) == f0 + 1 && measure(2, 3, species, 1) == either0 + 2 &&
                    measure(4, 2, species) == kind0 + 2 && measure(4, 19, expansion) == exp0 + 2;
            check("trigger 5: goals h and i each put a zebra (a male, a female) in a crate 3-20 tiles from the entrance (" +
                      std::to_string(placed) + std::to_string(near) + "); rules 2/0-3 and 4/2, 4/19 count them (" +
                      std::to_string(kinds) + ")",
                  placed && near && kinds);
            check("trigger 5: the first lands in the nearest ring with a full-score tile (ring " + std::to_string(gotRing) +
                      ", wanted " + std::to_string(wantRing) + ")",
                  wantRing < 0 || gotRing == wantRing);
          } else {
            check("scenario 4 in the list", false);
          }
          if (const ScenarioInfo *s32 = find("scenario/scn32/scn32.scn")) {
            startScenario(rm, *s32);
            UiElement *sell = g_hud ? g_hud->getElementById(3111) : nullptr;
            check("trigger 9: scenario 32's [start] disable_animalsell greys Sell Animal", sell && sell->isDisabled());
            const int manatee = 5416;
            const bool two = g_goals.getHooks().giveAnimal && g_goals.getHooks().giveAnimal(manatee, 0) &&
                             g_goals.getHooks().giveAnimal(manatee, 1);
            SDL_Log("[ScenarioChecks] manatees given: %d", two ? 1 : 0);
            const int with = measure(2, 0, manatee);
            // (REMOVE_MANATEE, 8 / 3 / 5416, comes after the gift and the care
            // chain in scenario 32's in-order chain: each met here by hand,
            // which gives a third manatee on the way)
            for (const char *name : {"DONATE_MANATEE_MALE", "EXHIBIT_75_MANATEE", "RESEARCH_MANATEE", "REMOVE_MANATEE"})
              for (Goals::Goal &goal : g_goals.mutableAll())
                if (goal.name == name) {
                  goal.rulea = 99;
                  goal.arga = 1;
                  goal.value = 1;
                  goal.type = 0;
                }
            for (int pass = 0; pass < 4; pass++) {
              g_goals.evaluateAll();
              while (g_hud->hasDialog())
                g_hud->answerDialog(true);
            }
            const int without = measure(2, 0, manatee);
            check("trigger 8: scenario 32's REMOVE_MANATEE takes away the manatees (" + std::to_string(with) + " -> " +
                      std::to_string(without) + ", given " + std::to_string(two) + ")",
                  two && with >= 2 && without == 0);
          } else {
            check("scenario 32 in the list", false);
          }
          {
            const std::string keepMap = g_currentMapPath;
            g_currentMapPath = "freeform/med_kids.scn";
            startGoals(rm, g_currentMapPath);
            size_t programs = 0, done = 0;
            for (ResearchBranch &b : Research::get().branches())
              for (ResearchCategory &c : b.categories)
                for (ResearchProgram &p : c.programs) {
                  programs++;
                  done += p.done ? 1 : 0;
                }
            check("trigger 10: the Kids' map starts with every research program done (" + std::to_string(done) + " of " +
                      std::to_string(programs) + ")",
                  programs > 0 && done == programs);
            g_currentMapPath = keepMap;
            Research::get().restart();
          }
        }
        // A saved scenario put back runs its [resume] triggers, not [start]
        // (zoo.exe resumeScript 0x58fb34), and keeps what the goals made
        // available (the types' flags): scenario 2's [start] unlocks the lion
        // (lion_unlocked) and [resume] leaves that out
        if (const ScenarioInfo *s02 = find("scenario/scn02/scn02.scn")) {
          startScenario(rm, *s02);
          const CatalogItem *lion = ItemCatalog::get().findName(5006);
          const bool startedWithLion = lion && ItemCatalog::get().isAvailable(*lion);
          std::filesystem::create_directories("B:/zt1-shots/r16-goals");
          const std::string saved = "B:/zt1-shots/r16-goals/resume-check.zt1save";
          const bool wrote = SaveGame::save(saved, *g_world, g_sim, *g_hud, g_currentMapPath);
          // (the part the save keeps is all that gives the lion back: the
          // game's flags are cleared and the world's UI reset by the load)
          ItemCatalog::get().clearAvailable();
          const bool loaded = wrote && gameLoad(rm, saved);
          const CatalogItem *lionAfter = ItemCatalog::get().findName(5006);
          UiElement *construct = g_hud ? g_hud->getElementById(1002) : nullptr;
          check(std::string("scenario 2 saved (") + (wrote ? "ok" : "failed") + ") and loaded: [resume] runs (Construct greyed by "
                "disableUI), the lion kept available (" + std::to_string(startedWithLion) + " at the start)",
                startedWithLion && loaded && construct && construct->isDisabled() && lionAfter &&
                    ItemCatalog::get().isAvailable(*lionAfter));
          if (const ScenarioInfo *s32b = find("scenario/scn32/scn32.scn")) {
            startScenario(rm, *s32b);
            const bool wrote32 = SaveGame::save(saved, *g_world, g_sim, *g_hud, g_currentMapPath);
            const bool loaded32 = wrote32 && gameLoad(rm, saved);
            UiElement *sell = g_hud ? g_hud->getElementById(3111) : nullptr;
            check("scenario 32 saved and loaded: its [resume] disable_animalsell greys Sell Animal again",
                  loaded32 && sell && sell->isDisabled());
          }
          std::error_code removed;
          std::filesystem::remove(saved, removed);
        } else {
          check("scenario 2 in the list", false);
        }
        // Freeform: every map starts with hideall (4/9); with every item
        // from the start (the switch) the catalogue's whole, with timed
        // unlocks (the original) empty until the first check's month-0
        // goal (unlock02's DEFAULT_ITEMS and the like)
        {
          const bool keepAll = Features::allUnlocked;
          const std::string keepMap = g_currentMapPath;
          g_currentMapPath = "freeform/deathmtn.scn";
          Features::allUnlocked = true;
          ItemCatalog::get().setMonth(unlockMonthFor(0));
          startGoals(rm, g_currentMapPath);
          {
            UiElement *button = g_hud ? g_hud->getElementById(4107) : nullptr;
            check("the Scenario information button is greyed in a freeform zoo", button && button->isDisabled());
          }
          const size_t all = ItemCatalog::get().inCategory("animals").size();
          Features::allUnlocked = false;
          ItemCatalog::get().setMonth(unlockMonthFor(0));
          startGoals(rm, g_currentMapPath);
          // (but the rewards kept: scenario 26's Deinosuchus, just above)
          size_t before = 0;
          for (const CatalogItem *a : ItemCatalog::get().inCategory("animals"))
            before += a->unlockMonth >= 0 ? 1 : 0;
          g_goals.evaluateAll();
          while (g_hud->hasDialog())
            g_hud->answerDialog(true);
          const size_t after = ItemCatalog::get().inCategory("animals").size();
          check("freeform (hideall at the start): every item on, " + std::to_string(all) + " animals; timed, " +
                    std::to_string(before) + " then " + std::to_string(after) + " after the first check",
                all > 50 && before == 0 && after > 0 && after < all);
          Features::allUnlocked = keepAll;
          g_currentMapPath = keepMap;
          ItemCatalog::get().setMonth(unlockMonthFor(0));
        }
      } else {
        check("Small Zoo and tutorial 1 in the list", false);
      }
      Settings::setInt("scenario", "ba", keptBa);
      Settings::setInt("scenario", "aa", keptAa);
      Settings::setInt("user", "unlockCount", keptCount);
      sm->syncWithDatabase();
    }
    rep << "\n" << (count - bad) << "/" << count << " pass\n";
    return bad ? 1 : 0;
  }
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
        t->edgeBits = 0;    // (its sides' cliff codes, byte 6, cleared too: flat ground has none)
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
      {5, "#1507", "gameopts_sound"}, {5, "#1508", "gameopts_help"},
      {5, "#1530", "gameopts_advanced"}, {5, "#1509", "gameopts_extras"},
      {0, "~esc", "escmenu"},
      {14, "#4104", "zoo_info"},   {14, "#4154", "zoo_finance"},
      {14, "#4153", "zoo_graphs"}, {14, "#4105", "zoo_awards"},
      {14, "#4180", "zoo_commerce"}, {14, "#4181", "zoo_research"},
      {14, "#4153", "zoo_graphs"},   {14, "#13982", "zoo_donations"},
      {14, "#13983", "zoo_profit"},  {14, "#13984", "zoo_attendance"},
      {14, "#4106", "zoo_rating"},   {14, "#4152", "zoo_rating_bar"},
      {803, "", "scenario_page"}};
  // Draws a few frames (art loads and settles) and saves the last
  auto capture = [&](const char *name) {
    for (int frame = 0; frame < 3; frame++) {
      SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
      SDL_RenderClear(renderer);
      world->setView(gameViewRect(renderer), getUiTransform(renderer).scale);
      world->draw(renderer);
      GraphicsEffects::applyMap(renderer); // (as the game's loop: GraphicsEffects.hpp)
      drawHud(renderer, rm);
      drawToolCost(renderer, rm);
      // (the Options window over it, as the game's loop draws it)
      if (g_hud->optionsOpen())
        drawOptions(renderer, rm, g_hud->options);
      drawDialogOverOptions(renderer, rm);
      if (frame < 2)
        SDL_RenderPresent(renderer);
    }
    int w = 0, h = 0;
    SDL_GetCurrentRenderOutputSize(renderer, &w, &h);
    SDL_Surface *s =
        SDL_CreateSurface(w, h, SDL_PIXELFORMAT_ARGB8888);
    if (s && ZT_RenderReadPixels(renderer, nullptr, SDL_PIXELFORMAT_ARGB8888,
                                  s->pixels, s->pitch) == 0)
      if (std::getenv("ZT_HUD_CLOCK_CHECKS") || std::getenv("ZT_SPEED_HUD_CHECKS")) IMG_SavePNG(s, (outDir + "/" + name + ".png").c_str());
      else SDL_SaveBMP(s, (outDir + "/" + name + ".bmp").c_str());
    if (s)
      SDL_DestroySurface(s);
    SDL_RenderPresent(renderer);
    note(name);
  };
  // ZT_SOAK=<game years> (with ZT_FLAT_LAB=1 on a blank freeform map such as
  // sm_aqua; ZT_SOAK_NIGHT=1: day/night on; ZT_SOAK_DT=<seconds a step>,
  // default 1/30): ours - a zoo with land and show-tank animals, staff,
  // guests and buildings played at the fastest speed's rate for that many
  // game years, as the game's loop plays it (the world, the books, the
  // income and the HUD's counts). Once a game month: the working set, the
  // books, the counts, units that stood still all month and guests that
  // never left, written to <out>/soak.txt. FAIL lines are findings.
  if (const char *soakYears = std::getenv("ZT_SOAK")) {
    std::ofstream log(outDir + "/soak.txt");
    static const bool noKeys[SDL_SCANCODE_COUNT] = {};
    const int years = std::max(1, std::atoi(soakYears));
    const bool night = std::getenv("ZT_SOAK_NIGHT") != nullptr;
    const float dt = std::getenv("ZT_SOAK_DT") ? static_cast<float>(std::atof(std::getenv("ZT_SOAK_DT"))) : 1.0f / 30.0f;
    Features::dayNightLighting = night;
    int findings = 0;
    auto finding = [&](const std::string &what) {
      findings++;
      log << "FAIL " << what << "\n";
      log.flush();
    };
    extern std::string prepareMarineSoak(World &, ResourceManager *);
    Fences &sf = world->getFences();
    Animals &sa = world->getAnimals();
    Staff &ss = world->getStaff();
    WorldMap &smap = world->getMap();
    sf.setEditing(true); // (fixture construction, outside the zoo's ownership checks)
    for (auto &b : Research::get().branches())
      for (auto &c : b.categories)
        for (auto &p : c.programs)
          if (p.effect == 6)
            p.done = true;
    g_sim.setCash(1000000);
    world->setPaused(false);
    for (int f = 0; f < 3; f++)
      world->update(noKeys, 1.0f / 60.0f);
    log << prepareMarineSoak(*world, rm) << "\n";
    // a land exhibit: chain-link, four zebras and a keeper (ZT_SOAK_LAND=1: the
    // fence laid piece by piece doesn't make an exhibit the animals live in yet,
    // so the land animals are tested by the tour instead, ZT_TOUR_SAVE)
    if (std::getenv("ZT_SOAK_LAND")) {
      // (a 14 x 14 place wholly inside the zoo, clear of the tanks' ground)
      int x = -1, y = -1;
      const int w = 14, h = 14, type = sf.typeIndex("chainlnk");
      for (int sy = 3; sy < smap.getHeight() - h - 3 && x < 0; sy++)
        for (int sx = 3; sx < smap.getWidth() - w - 3 && x < 0; sx++) {
          if (sx + w + 2 > 18 && sx - 1 < 66 && sy + h + 2 > 18 && sy - 1 < 52)
            continue;
          bool clear = true;
          for (int yy = sy - 2; yy <= sy + h + 2 && clear; yy++)
            for (int xx = sx - 2; xx <= sx + w + 2 && clear; xx++)
              clear = sf.insideZoo(xx, yy);
          if (clear) {
            x = sx;
            y = sy;
          }
        }
      log << "land exhibit site " << x << "," << y << "\n";
      for (int yy = y - 1; yy <= y + h; ++yy)
        for (int xx = x - 1; xx <= x + w; ++xx)
          smap.setPath(xx, yy, "");
      for (int k = 0; k < w; ++k) {
        sf.place({true, x + k, y}, type, 3000, smap);
        sf.place({true, x + k, y + h}, type, 3001, smap);
      }
      for (int k = 0; k < h; ++k) {
        sf.place({false, x, y + k}, type, 3002, smap);
        sf.place({false, x + w, y + k}, type, 3003, smap);
      }
      for (int id : sf.updateExhibits(smap, 1, 0, 1))
        sf.finishExhibit(id, "Soak savannah", smap);
      for (int yy = y; yy < y + h; ++yy)
        for (int xx = x; xx < x + w; ++xx)
          smap.setPath(xx, yy, "");
      const int zebra = sa.typeOfFile("animals/zebra.ai");
      for (int k = 0; k < 4; ++k)
        sa.adopt(zebra, k % 2, x + 3.5f + k * 2, y + 7.5f, smap, sf, true);
      const int keeper = ss.hire(ss.typeOfFile("staff/keeper.ai"), x - 1.5f, y + 7.5f, smap, sf);
      const int landExhibit = sf.exhibitAt(x, y);
      if (keeper >= 0 && landExhibit >= 0)
        ss.assign(keeper, landExhibit, sf);
      log << "land exhibit " << landExhibit << ", keeper " << keeper << "\n";
    }
    sf.setEditing(false);
    world->reindex();
    log << "setup: " << sa.members().size() << " animals, " << ss.members().size() << " staff, "
        << world->getObjects().objects().size() << " objects, day/night " << (night ? "on" : "off") << ", step " << dt
        << " s\n";
    log.flush();
    extern double processWorkingSetMb();
    auto workingSet = [] { return processWorkingSetMb(); };
    const int startMonth = g_sim.year() * 12 + g_sim.month();
    int lastMonth = startMonth - 1;
    struct Pos {
      float x, y;
    };
    std::map<int, Pos> staffAt, animalAt;
    std::map<int, int> guestFirst; // guest id -> the month first seen
    double firstMb = 0, lastMb = 0;
    const Uint64 t0 = SDL_GetPerformanceCounter();
    long steps = 0;
    while (g_sim.year() * 12 + g_sim.month() < startMonth + years * 12) {
      g_world->objectsMutable().today = g_sim.today();
      g_world->getFences().today = g_sim.today();
      world->update(noKeys, dt, dt);
      g_sim.update(dt);
      worldInputs(renderer, rm, std::vector<Input>());
      steps++;
      if (g_hud->hasDialog()) {
        // (a message or question the game brought up, answered)
        Input in{};
        in.type = InputType::BUTTON;
        in.event = InputEvent::KEY_DOWN;
        in.key = SDLK_RETURN;
        std::vector<Input> inputs = {in};
        g_hud->handleInputs(inputs);
      }
      if (steps % 300 == 0)
        g_rateNow = true;
      const int month = g_sim.year() * 12 + g_sim.month();
      if (month == lastMonth)
        continue;
      lastMonth = month;
      // a month's end: look
      world->centreOn(30, 30);
      world->setView(gameViewRect(renderer), getUiTransform(renderer).scale);
      world->draw(renderer);
      SDL_RenderPresent(renderer);
      const double mb = workingSet();
      if (firstMb == 0)
        firstMb = mb;
      lastMb = mb;
      int stillStaff = 0, stillAnimals = 0, oldGuests = 0, loose = 0;
      std::map<int, Pos> nowStaff, nowAnimals;
      for (const Staff::Member &m : ss.members()) {
        nowStaff[m.id] = {m.x, m.y};
        auto p = staffAt.find(m.id);
        if (p != staffAt.end() && p->second.x == m.x && p->second.y == m.y)
          stillStaff++;
      }
      float worstHunger = 0;
      for (const Animals::Member &m : sa.members()) {
        nowAnimals[m.id] = {m.x, m.y};
        auto p = animalAt.find(m.id);
        if (p != animalAt.end() && p->second.x == m.x && p->second.y == m.y)
          stillAnimals++;
        loose += m.escaped ? 1 : 0;
        worstHunger = std::max(worstHunger, m.hunger);
      }
      for (const Guests::Guest &g : world->getGuests().guests()) {
        auto f = guestFirst.emplace(g.id, month).first;
        if (month - f->second >= 4)
          oldGuests++;
      }
      char line[400];
      std::snprintf(line, sizeof line,
                    "y%d m%02d  %.0f MB  cash %.0f  rating %.0f  guests %zu  animals %zu (worst hunger %.0f, %d loose)  staff %zu  "
                    "still: staff %d animals %d  guests over 4 months %d  [%.0f s]\n",
                    g_sim.year(), g_sim.month() + 1, mb, g_sim.cash(), g_sim.rating(), world->getGuests().guests().size(),
                    sa.members().size(), worstHunger, loose, ss.members().size(), stillStaff, stillAnimals, oldGuests,
                    (SDL_GetPerformanceCounter() - t0) / static_cast<double>(SDL_GetPerformanceFrequency()));
      log << line;
      // (who: the starving, the ones that never moved, the guests that stayed)
      {
        int shown = 0;
        for (const Animals::Member &m : sa.members()) {
          auto p = animalAt.find(m.id);
          const bool still = p != animalAt.end() && p->second.x == m.x && p->second.y == m.y;
          if ((m.hunger >= 95 || still || m.health < 50) && shown++ < 8)
            log << "    animal " << m.name << " (" << sa.types()[m.type].key << ") exhibit " << m.exhibit << " hunger " << std::lround(m.hunger)
                << " health " << std::lround(m.health) << " anim " << m.anim << (still ? " STILL" : "") << (m.boxed ? " boxed" : "") << " at "
                << std::lround(m.x) << "," << std::lround(m.y) << "\n";
        }
        for (const Staff::Member &m : ss.members()) {
          auto p = staffAt.find(m.id);
          if (p != staffAt.end() && p->second.x == m.x && p->second.y == m.y)
            log << "    staff " << m.name << " (" << ss.types()[m.type].key << ") job " << static_cast<int>(m.job) << " anim " << m.anim << " STILL at "
                << std::lround(m.x) << "," << std::lround(m.y) << "\n";
        }
        for (const Guests::Guest &g : world->getGuests().guests())
          if (month - guestFirst[g.id] >= 4)
            log << "    guest " << g.id << " state " << static_cast<int>(g.state) << " anim " << g.anim << " target " << g.target << " at "
                << std::lround(g.x) << "," << std::lround(g.y) << " first seen month " << guestFirst[g.id] - startMonth << " leaving " << g.leaving << " path " << g.pathAt << "/" << g.path.size()
                << " to " << (g.path.empty() ? -1 : std::lround(g.path.back().first)) << "," << (g.path.empty() ? -1 : std::lround(g.path.back().second))
                << " speed " << g.speed << " playFor " << g.playFor << " happiness " << std::lround(g.happiness) << " hunger " << std::lround(g.hunger)
                << " thirst " << std::lround(g.thirst) << " bathroom " << std::lround(g.bathroom) << " tired " << std::lround(g.tired) << "\n";
      }
      staffAt = nowStaff;
      animalAt = nowAnimals;
      log.flush();
      if (month - startMonth >= 2 && stillStaff > 0 && stillStaff == static_cast<int>(ss.members().size()))
        finding("every staff member stood still for a month at y" + std::to_string(g_sim.year()) + " m" +
                std::to_string(g_sim.month() + 1));
      if (oldGuests > 0)
        finding(std::to_string(oldGuests) + " guests inside for over 4 months at y" + std::to_string(g_sim.year()) + " m" +
                std::to_string(g_sim.month() + 1));
      if (loose > 0)
        finding(std::to_string(loose) + " animals loose at y" + std::to_string(g_sim.year()) + " m" +
                std::to_string(g_sim.month() + 1));
    }
    if (lastMb > firstMb * 1.5 + 100)
      finding("working set grew from " + std::to_string(static_cast<int>(firstMb)) + " to " +
              std::to_string(static_cast<int>(lastMb)) + " MB");
    log << "DONE " << years << " game years, " << steps << " steps, " << findings << " findings\n";
    log.close();
    if (timings)
      std::fclose(timings);
    return findings ? 1 : 0;
  }
  // ZT_FREEFORM_TOUR=1: every freeform map in turn, started as the freeform
  // menu starts one, looked at, and a tiny zoo made on it with the player's
  // own tools - a chain-link exhibit (named), a path to it from the
  // entrance, a zebra, a keeper, a food stand - then a minute of it played.
  // Nothing is saved. A line a map in <out>/freeform-tour.txt (PASS, or FAIL
  // and what failed), its pictures beside it. ZT_TOUR_FROM=<n> starts at the
  // n-th map (after a crash, the next one on); ZT_TOUR_ONLY=<stem> one map
  if (std::getenv("ZT_FREEFORM_TOUR")) {
    std::ofstream tour(outDir + "/freeform-tour.txt", std::ios::app);
    const int from = std::getenv("ZT_TOUR_FROM") ? std::atoi(std::getenv("ZT_TOUR_FROM")) : 0;
    const char *only = std::getenv("ZT_TOUR_ONLY");
    static const bool noKeys[SDL_SCANCODE_COUNT] = {};
    int failedMaps = 0, maps = 0;
    for (int i = 0; const FreeformMap *fm = sm->getFreeformMap(i); i++) {
      const std::string stem = getFileStem(fm->path);
      if (i < from || (only && stem != only))
        continue;
      tour << "START " << i << " " << stem << " (" << fm->name << ")\n";
      tour.flush();
      maps++;
      std::vector<std::string> failed;
      std::ostringstream info;
      auto need = [&](bool ok, const std::string &what) {
        if (!ok)
          failed.push_back(what);
        return ok;
      };
      const bool loaded = world->loadFreeform(fm->path);
      g_currentMapPath = fm->path;
      createHud(rm, world, CASH_START);
      startGoals(rm, fm->path);
      world->edgeScroll = false;
      world->setPaused(false);
      WorldMap &map = world->getMap();
      Fences &fences = world->getFences();
      int ex = -1, ey = -1;
      // (a few of its first moments: the game finds the entrance in its first)
      for (int f = 0; f < 3; f++)
        world->update(noKeys, 1.0f / 60.0f);
      const bool mapOk = need(loaded && map.getWidth() > 0 && map.getHeight() > 0, "the map loads");
      const bool gateOk = need(world->getGuests().entrance(ex, ey), "it has an entrance");
      info << "size " << map.getWidth() << "x" << map.getHeight() << ", entrance " << ex << "," << ey;
      // (any message or dialog the start brought up answered first)
      auto settle = [&] {
        for (int k = 0; k < 4 && g_hud && g_hud->hasDialog(); k++) {
          Input in{};
          in.type = InputType::BUTTON;
          in.event = InputEvent::KEY_DOWN;
          in.key = SDLK_RETURN;
          std::vector<Input> inputs = {in};
          g_hud->handleInputs(inputs);
        }
      };
      settle();
      auto show = [&](float x, float y) {
        world->centreOn(x, y);
        world->setView(gameViewRect(renderer), getUiTransform(renderer).scale);
        world->draw(renderer);
      };
      if (gateOk)
        show(ex + 0.5f, ey + 0.5f);
      capture((stem + "-1-loaded").c_str());
      const double cashStart = g_sim.cash();
      if (mapOk && gateOk) {
        // Zoo ground walkable from the entrance: inside the zoo, not water,
        // in no exhibit, a path or room for one, each step level with the
        // last (the route the path takes, by parent)
        const int W = map.getWidth(), H = map.getHeight();
        auto open = [&](int x, int y) {
          const MapTile *t = map.getTile(x, y);
          // (a path is walked wherever it is - by the gate it can run along
          // the zoo's wall line; new ground must be zoo ground: canLayPath)
          return t && fences.exhibitAt(x, y) < 0 && t->terrainType != 9 && t->terrainType != 10 &&
                 (map.isPath(x, y) || world->canLayPath(x, y));
        };
        auto level = [&](int x, int y, int nx, int ny) {
          const MapTile *a = map.getTile(x, y), *b = map.getTile(nx, ny);
          int a0, a1, b0, b1;
          if (nx > x) { a0 = a->cornerHeight[CORNER_X1Y0]; a1 = a->cornerHeight[CORNER_X1Y1]; b0 = b->cornerHeight[CORNER_X0Y0]; b1 = b->cornerHeight[CORNER_X0Y1]; }
          else if (nx < x) { a0 = a->cornerHeight[CORNER_X0Y0]; a1 = a->cornerHeight[CORNER_X0Y1]; b0 = b->cornerHeight[CORNER_X1Y0]; b1 = b->cornerHeight[CORNER_X1Y1]; }
          else if (ny > y) { a0 = a->cornerHeight[CORNER_X0Y1]; a1 = a->cornerHeight[CORNER_X1Y1]; b0 = b->cornerHeight[CORNER_X0Y0]; b1 = b->cornerHeight[CORNER_X1Y0]; }
          else { a0 = a->cornerHeight[CORNER_X0Y0]; a1 = a->cornerHeight[CORNER_X1Y0]; b0 = b->cornerHeight[CORNER_X0Y1]; b1 = b->cornerHeight[CORNER_X1Y1]; }
          return a0 == b0 && a1 == b1;
        };
        // (the zoo's own path picked first: where a path can go depends on it)
        std::string pathKey = "path";
        if (int pt = map.getPathType(ex, ey); pt >= 0 && pt < static_cast<int>(map.getPathTypes().size()))
          pathKey = map.getPathTypes()[pt];
        int pathCost = 0;
        for (const CatalogItem &item : ItemCatalog::get().all())
          if (item.file == "paths/" + pathKey + ".ai")
            pathCost = item.cost;
        world->setPathTool(pathKey, pathCost);
        std::vector<int> parent;
        std::vector<std::pair<int, int>> order;
        auto walk = [&] {
          parent.assign(static_cast<size_t>(W) * H, -2);
          order.clear();
          std::queue<std::pair<int, int>> q;
          parent[static_cast<size_t>(ey) * W + ex] = -1;
          q.push({ex, ey});
          while (!q.empty()) {
            auto [x, y] = q.front();
            q.pop();
            order.push_back({x, y});
            const int d4[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
            for (auto &d : d4) {
              const int nx = x + d[0], ny = y + d[1];
              if (nx < 0 || ny < 0 || nx >= W || ny >= H || parent[static_cast<size_t>(ny) * W + nx] != -2 ||
                  !open(nx, ny) || !level(x, y, nx, ny))
                continue;
              // (not through a fence: Arctic Maze's walls are fences - but along
              // the map's own path where it runs through a fence line, as
              // Sandy's entrance court does)
              if (const Fences::Piece *p = fences.at({nx == x, std::max(x, nx), std::max(y, ny)});
                  p && !p->gate && !(map.isPath(x, y) && map.isPath(nx, ny)))
                continue;
              parent[static_cast<size_t>(ny) * W + nx] = y * W + x;
              q.push({nx, ny});
            }
          }
        };
        walk();
        world->setPathTool("", 0);
        auto reached = [&](int x, int y) {
          return x >= 0 && y >= 0 && x < W && y < H && parent[static_cast<size_t>(y) * W + x] != -2;
        };
        info << ", " << order.size() << " tiles walkable from it";
        // (debug: the ground round the entrance - P path, . a path can go,
        // F not zoo ground the fences take, S too steep, T something in the
        // way, E an exhibit, # the entrance; lower case where reached)
        if (std::getenv("ZT_TOUR_DEBUG")) {
          world->setPathTool(pathKey, pathCost);
          for (int y = ey - 14; y <= ey + 14; y++) {
            std::string row;
            for (int x = ex - 20; x <= ex + 20; x++) {
              char c = ' ';
              if (map.getTile(x, y)) {
                c = x == ex && y == ey               ? '#'
                    : map.isPath(x, y)                ? 'P'
                    : fences.exhibitAt(x, y) >= 0     ? 'E'
                    : fences.tileFit(x, y, map) != Fences::Fit::Ok ? 'F'
                    : !map.pathShapeOk(x, y)          ? 'S'
                    : !world->canLayPath(x, y)        ? 'T'
                                                      : '.';
                if (reached(x, y) && c != '#')
                  c = static_cast<char>(std::tolower(c == '.' ? ',' : c));
              }
              row += c;
            }
            SDL_Log("[Tour] map %3d %s", y, row.c_str());
          }
          for (const auto &[e, p] : fences.pieces())
            if (std::abs(e.x - ex) <= 8 && std::abs(e.y - ey) <= 4)
              SDL_Log("[Tour] fence %s %d,%d %s%s", e.alongX ? "h" : "v", e.x, e.y,
                      fences.types()[p.type].key.c_str(), p.gate ? " gate" : "");
          world->setPathTool("", 0);
        }
        // (few: why each step from the entrance was refused)
        if (order.size() < 50) {
          const int d4[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
          for (auto &d : d4) {
            const int nx = ex + d[0], ny = ey + d[1];
            const MapTile *t = map.getTile(nx, ny);
            info << " [" << nx << "," << ny << " path " << map.isPath(nx, ny) << " zoo " << fences.insideZoo(nx, ny)
                 << " exhibit " << fences.exhibitAt(nx, ny) << " canPath " << world->canLayPath(nx, ny)
                 << " terrain " << (t ? int(t->terrainType) : -1) << " level " << (t ? level(ex, ey, nx, ny) : false) << "]";
          }
        }
        // (a corner or a tile where a click on it finds it: on the screen
        // where it is drawn, and not behind something nearer)
        auto vertexSeen = [&](int x, int y) {
          int wx, wy;
          float px, py;
          world->vertexToWindow(x, y, wx, wy);
          return world->pickWindow(wx, wy, px, py) && std::lround(px) == x && std::lround(py) == y;
        };
        auto tileSeen = [&](int x, int y) {
          int ax, ay, cx, cy;
          float px, py;
          world->vertexToWindow(x, y, ax, ay);
          world->vertexToWindow(x + 1, y + 1, cx, cy);
          return world->pickWindow((ax + cx) / 2, (ay + cy) / 2, px, py) && static_cast<int>(std::floor(px)) == x &&
                 static_cast<int>(std::floor(py)) == y;
        };
        // (the view turned until a tile is seen, as a player turns it)
        auto face = [&](int x, int y) {
          show(x + 0.5f, y + 0.5f);
          for (int r = 0; r < 3 && !tileSeen(x, y); r++) {
            world->rotateView(1);
            show(x + 0.5f, y + 0.5f);
          }
          return tileSeen(x, y);
        };
        // A 4 x 4 block, nearest the entrance first: level all over, its
        // ring walkable too, no path, fence or thing in or near it
        const int n = 4;
        int bx = -1, by = -1;
        for (auto [ox, oy] : order) {
          if (bx >= 0)
            break;
          const int x0 = ox - n / 2, y0 = oy - n / 2;
          if (std::abs(x0 - ex) < 4 && std::abs(y0 - ey) < 4)
            continue; // (not on the gate)
          // (slopes as fences take them, as a player builds: but no cliff
          // between any two of its tiles, and all of it zoo ground)
          bool ok = true;
          for (int y = y0 - 1; y <= y0 + n && ok; y++)
            for (int x = x0 - 1; x <= x0 + n && ok; x++) {
              ok = map.getTile(x, y) && reached(x, y) && !map.isPath(x, y) && fences.insideZoo(x, y);
              if (ok && x < x0 + n)
                ok = map.getTile(x + 1, y) && level(x, y, x + 1, y);
              if (ok && y < y0 + n)
                ok = map.getTile(x, y + 1) && level(x, y, x, y + 1);
              for (int e = 0; e < 2 && ok; e++)
                ok = !fences.at({e == 0, x, y}) && !fences.at({e == 0, x + 1, y + 1});
            }
          // (nothing standing in it or on its ring: trees around it are
          // fine, as a player fences among them)
          for (const PlacedObjects::Object &o : world->getObjects().objects())
            if (ok && o.x > x0 - 1 && o.x < x0 + n + 1 && o.y > y0 - 1 && o.y < y0 + n + 1)
              ok = false;
          // (and not plugging the only way into its own pocket - Arctic
          // Maze's corridors: some tile beside it still reached round it)
          if (ok) {
            std::vector<uint8_t> seen(static_cast<size_t>(W) * H, 0);
            std::queue<std::pair<int, int>> q;
            seen[static_cast<size_t>(ey) * W + ex] = 1;
            q.push({ex, ey});
            auto inBlock = [&](int x, int y) { return x >= x0 && x < x0 + n && y >= y0 && y < y0 + n; };
            bool beside = false;
            while (!q.empty() && !beside) {
              auto [x, y] = q.front();
              q.pop();
              beside = !inBlock(x, y) && x >= x0 - 1 && x <= x0 + n && y >= y0 - 1 && y <= y0 + n &&
                       (x >= x0 && x < x0 + n || y >= y0 && y < y0 + n);
              const int d4[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
              for (auto &d : d4) {
                const int nx = x + d[0], ny = y + d[1];
                if (!reached(nx, ny) || inBlock(nx, ny) || seen[static_cast<size_t>(ny) * W + nx] ||
                    !level(x, y, nx, ny))
                  continue;
                if (const Fences::Piece *p = fences.at({nx == x, std::max(x, nx), std::max(y, ny)});
                    p && !p->gate && !(map.isPath(x, y) && map.isPath(nx, ny)))
                  continue;
                seen[static_cast<size_t>(ny) * W + nx] = 1;
                q.push({nx, ny});
              }
            }
            ok = beside;
          }
          // (and its corners where a click finds them, from some side - in a
          // pit, a corner behind its front wall isn't: a player turns round)
          if (ok) {
            show(x0 + n / 2.0f, y0 + n / 2.0f);
            auto corners = [&] {
              return vertexSeen(x0, y0) && vertexSeen(x0 + n, y0) && vertexSeen(x0 + n, y0 + n) &&
                     vertexSeen(x0, y0 + n);
            };
            bool seen = corners();
            for (int r = 0; r < 3 && !seen; r++) {
              world->rotateView(1);
              show(x0 + n / 2.0f, y0 + n / 2.0f);
              seen = corners();
            }
            ok = seen;
          }
          if (ok) {
            bx = x0;
            by = y0;
          }
        }
        if (need(bx >= 0, "clear ground with no cliffs for a 4x4 exhibit near the entrance")) {
          info << ", exhibit at " << bx << "," << by;
          show(bx + n / 2.0f, by + n / 2.0f);
          auto window = [&](int x, int y, int &wx, int &wy) {
            int ax, ay, cx, cy;
            const bool a = world->vertexToWindow(x, y, ax, ay), c = world->vertexToWindow(x + 1, y + 1, cx, cy);
            wx = (ax + cx) / 2;
            wy = (ay + cy) / 2;
            return a && c;
          };
          auto clickTile = [&](int x, int y) {
            face(x, y);
            int wx, wy;
            window(x, y, wx, wy);
            world->mouseMove(wx, wy);
            handleToolResult(world->mouseDown(wx, wy), rm);
            handleToolResult(world->mouseUp(wx, wy), rm);
          };
          // The exhibit: chain-link round it, a side at a time, named
          const size_t exhibitsBefore = fences.exhibits().size();
          world->setFenceTool(fences.typeIndex("chainlnk"));
          const std::pair<int, int> loop[] = {{bx, by}, {bx + n, by}, {bx + n, by + n}, {bx, by + n}, {bx, by}};
          const bool debug = std::getenv("ZT_TOUR_DEBUG") != nullptr;
          if (debug)
            for (int y = by - 1; y <= by + n; y++) {
              std::ostringstream row;
              for (int x = bx - 1; x <= bx + n; x++) {
                const MapTile *t = map.getTile(x, y);
                row << " " << (t ? t->cornerHeight[0] : -99) << "/" << (t ? t->cornerHeight[1] : -99) << "/"
                    << (t ? t->cornerHeight[2] : -99) << "/" << (t ? t->cornerHeight[3] : -99);
              }
              SDL_Log("[Tour] heights row %d:%s", y, row.str().c_str());
            }
          for (int s = 0; s < 4; s++) {
            int x0, y0, x1, y1;
            world->vertexToWindow(loop[s].first, loop[s].second, x0, y0);
            world->vertexToWindow(loop[s + 1].first, loop[s + 1].second, x1, y1);
            handleToolResult(world->mouseDown(x0, y0), rm);
            world->mouseMove(x1, y1);
            if (debug) {
              float px0, py0, px1, py1;
              world->pickWindow(x0, y0, px0, py0);
              world->pickWindow(x1, y1, px1, py1);
              std::string pv;
              for (auto &pp : fences.preview)
                pv += std::string(" ") + (pp.first.alongX ? "x" : "y") + std::to_string(pp.first.x) + "," + std::to_string(pp.first.y);
              SDL_Log("[Tour] side %d: %d,%d -> %d,%d at window %d,%d -> %d,%d picked %.2f,%.2f -> %.2f,%.2f preview:%s", s,
                      loop[s].first, loop[s].second, loop[s + 1].first, loop[s + 1].second, x0, y0, x1, y1, px0, py0, px1,
                      py1, pv.c_str());
            }
            handleToolResult(world->mouseUp(x1, y1), rm);
          }
          world->setFenceTool(-1);
          settle();
          const int exhibitId = fences.exhibitAt(bx + 1, by + 1);
          const Fences::Exhibit *made = fences.exhibit(exhibitId);
          need(made && made->tiles.size() == size_t(n * n) && fences.exhibits().size() > exhibitsBefore,
               "the fence loop makes an exhibit of 16 tiles");
          // A path from the entrance to the tile below its corner (the way
          // found again: the exhibit now stands where it may have gone)
          // (its front: the first tile beside it the entrance still reaches -
          // in a maze's corridor the exhibit cuts one side off)
          int tx = bx, ty = by + n;
          world->setPathTool(pathKey, pathCost);
          walk();
          for (int k = 0; k < 4 * n && !reached(tx, ty); k++) {
            const int s = k / n, i = k % n;
            tx = s == 0 ? bx + i : s == 1 ? bx + n : s == 2 ? bx + i : bx - 1;
            ty = s == 0 ? by + n : s == 1 ? by + i : s == 2 ? by - 1 : by + i;
          }
          if (debug && !reached(tx, ty))
            for (int k = 0; k < 4 * n; k++) {
              const int s = k / n, i = k % n;
              const int rx = s == 0 ? bx + i : s == 1 ? bx + n : s == 2 ? bx + i : bx - 1;
              const int ry = s == 0 ? by + n : s == 1 ? by + i : s == 2 ? by - 1 : by + i;
              SDL_Log("[Tour] beside %d,%d: exhibit %d canPath %d path %d", rx, ry, fences.exhibitAt(rx, ry),
                      world->canLayPath(rx, ry) ? 1 : 0, map.isPath(rx, ry) ? 1 : 0);
            }
          need(reached(tx, ty), "the exhibit's front reachable from the entrance");
          std::vector<std::pair<int, int>> route;
          for (int at = ty * W + tx; at >= 0; at = parent[static_cast<size_t>(at)])
            route.push_back({at % W, at / W});
          const std::string &key = pathKey;
          world->setPathTool(pathKey, pathCost);
          int laid = 0, missing = 0;
          for (auto it = route.rbegin(); it != route.rend(); ++it) {
            if (map.isPath(it->first, it->second))
              continue;
            show(it->first + 0.5f, it->second + 0.5f);
            clickTile(it->first, it->second);
            if (map.isPath(it->first, it->second))
              laid++;
            else
              missing++;
          }
          world->setPathTool("", 0);
          info << ", path " << key << " " << laid << " tiles laid";
          need(map.isPath(tx, ty) && missing == 0,
               "a path laid from the entrance to the exhibit (" + std::to_string(missing) + " tiles refused)");
          // A zebra in it, a keeper by it, a food stand beside the path
          show(bx + n / 2.0f, by + n / 2.0f);
          Animals &animals = world->getAnimals();
          const size_t animalsBefore = animals.members().size();
          world->setAnimalTool(animals.typeOfFile("animals/zebra.ai"), true);
          clickTile(bx + 1, by + 1);
          world->setAnimalTool(-1, false);
          const bool adopted = need(animals.members().size() == animalsBefore + 1 &&
                                        animals.members().back().exhibit == exhibitId,
                                    "a zebra adopted into the exhibit");
          const int zebra = adopted ? animals.members().back().id : -1;
          Staff &staff = world->getStaff();
          const size_t staffBefore = staff.members().size();
          world->setStaffTool(staff.typeOfFile("staff/keeper.ai"));
          clickTile(tx, ty);
          world->setStaffTool(-1);
          need(staff.members().size() == staffBefore + 1, "a keeper hired by the exhibit");
          int standCost = 0;
          const std::string stand = "scenery/building/hdogstnd.ai";
          for (const CatalogItem &item : ItemCatalog::get().all())
            if (item.file == stand)
              standCost = item.cost;
          world->setObjectTool(stand, standCost, 0);
          const size_t objectsBefore = world->getObjects().objects().size();
          for (int r = 1; r <= 3 && world->getObjects().objects().size() == objectsBefore; r++)
            for (int dy = -r; dy <= r && world->getObjects().objects().size() == objectsBefore; dy++)
              for (int dx = -r; dx <= r && world->getObjects().objects().size() == objectsBefore; dx++) {
                const int x = tx + dx, y = ty + dy;
                if (!reached(x, y) || map.isPath(x, y) || fences.exhibitAt(x, y) >= 0)
                  continue;
                clickTile(x, y);
              }
          world->setObjectTool("", 0, 0);
          need(world->getObjects().objects().size() == objectsBefore + 1, "a food stand built beside the path");
          info << ", spent $" << std::lround(cashStart - g_sim.cash());
          // A minute of it played
          for (int f = 0; f < 3600; f++)
            world->update(noKeys, 1.0f / 60.0f);
          if (zebra >= 0) {
            const Animals::Member *z = nullptr;
            for (const Animals::Member &m : animals.members())
              if (m.id == zebra)
                z = &m;
            need(z && z->exhibit == exhibitId && !z->escaped, "after a minute the zebra is still in its exhibit");
          }
          info << ", after a minute " << world->getGuests().guests().size() << " guests";
          // ours: ZT_TOUR_SAVE=<game months>: the tiny zoo played that many
          // game months (the books and the date moving), saved as a shared
          // .zoo and loaded back; the counts, the money, the date and the
          // books must come back as they were, and it must play on another
          // month. <out>/<map>.zoo is kept for ZT_TOUR_KEEP=<stem,stem>, else
          // removed.
          if (const char *roundMonths = std::getenv("ZT_TOUR_SAVE")) {
            struct Snap {
              size_t animals = 0, staff = 0, guests = 0, objects = 0, exhibits = 0, history = 0;
              double cash = 0, rating = 0, monthTotal = 0;
              int year = 0, month = 0, day = 0;
            };
            auto snap = [&] {
              Snap s;
              s.animals = g_world->getAnimals().members().size();
              s.staff = g_world->getStaff().members().size();
              s.guests = g_world->getGuests().guests().size();
              s.objects = g_world->getObjects().objects().size();
              s.exhibits = g_world->getFences().exhibits().size();
              s.history = g_sim.history().size();
              s.cash = g_sim.cash();
              s.rating = g_sim.rating();
              s.monthTotal = g_sim.history().empty() ? 0 : g_sim.history().back().total();
              s.year = g_sim.year();
              s.month = g_sim.month();
              s.day = g_sim.dayOfMonth();
              return s;
            };
            auto play = [&](int months) {
              const int steps = months * 3600;
              for (int k = 0; k < steps; k++) {
                g_world->update(noKeys, 0.1f, 0.1f);
                g_sim.update(0.1f);
                worldInputs(renderer, rm, std::vector<Input>());
                if (k % 100 == 0)
                  settle();
              }
            };
            play(std::max(1, std::atoi(roundMonths)));
            const Snap before = snap();
            const std::string zooFile = outDir + "/" + stem + ".zoo";
            const bool saved = gameSave(zooFile);
            const bool loaded = need(saved, "the zoo saves") && need(gameLoad(rm, zooFile), "the saved zoo loads back");
            if (loaded) {
              world = g_world;
              settle();
              const Snap after = snap();
              need(before.animals == after.animals, "animals after the round trip: " + std::to_string(before.animals) + " -> " + std::to_string(after.animals));
              need(before.staff == after.staff, "staff after the round trip: " + std::to_string(before.staff) + " -> " + std::to_string(after.staff));
              need(before.guests == after.guests, "guests after the round trip: " + std::to_string(before.guests) + " -> " + std::to_string(after.guests));
              need(before.objects == after.objects, "objects after the round trip: " + std::to_string(before.objects) + " -> " + std::to_string(after.objects));
              need(before.exhibits == after.exhibits, "exhibits after the round trip: " + std::to_string(before.exhibits) + " -> " + std::to_string(after.exhibits));
              need(before.history == after.history, "months on the books after the round trip: " + std::to_string(before.history) + " -> " + std::to_string(after.history));
              need(std::fabs(before.cash - after.cash) < 0.01, "cash after the round trip: " + std::to_string(before.cash) + " -> " + std::to_string(after.cash));
              need(std::fabs(before.monthTotal - after.monthTotal) < 0.01, "this month's books after the round trip: " + std::to_string(before.monthTotal) + " -> " + std::to_string(after.monthTotal));
              need(before.year == after.year && before.month == after.month && before.day == after.day,
                   "the date after the round trip: " + std::to_string(before.day) + "/" + std::to_string(before.month + 1) + "/" + std::to_string(before.year) +
                       " -> " + std::to_string(after.day) + "/" + std::to_string(after.month + 1) + "/" + std::to_string(after.year));
              info << ", saved and loaded back (" << after.animals << " animals, " << after.staff << " staff, " << after.guests << " guests, cash "
                   << std::lround(after.cash) << ", rating " << std::lround(before.rating) << " -> " << std::lround(after.rating) << ")";
              play(1);
              info << ", played on a month (" << g_world->getGuests().guests().size() << " guests)";
            }
            bool keep = false;
            if (const char *k = std::getenv("ZT_TOUR_KEEP"))
              keep = (std::string(",") + k + ",").find("," + stem + ",") != std::string::npos;
            std::error_code ec;
            if (!keep)
              std::filesystem::remove(zooFile, ec);
          }
          show(bx + n / 2.0f, by + n / 2.0f);
        }
      }
      capture((stem + "-2-zoo").c_str());
      if (!failed.empty())
        failedMaps++;
      tour << (failed.empty() ? "PASS " : "FAIL ") << i << " " << stem << " (" << fm->name << "): " << info.str();
      for (const std::string &f : failed)
        tour << "\n     failed: " << f;
      tour << "\n";
      tour.flush();
    }
    tour << "DONE " << maps << " maps, " << failedMaps << " with failures\n";
    if (timings)
      std::fclose(timings);
    return failedMaps ? 1 : 0;
  }
  if (std::getenv("ZT_SPEED_HUD_CHECKS")) {
    SDL_HideWindow(SDL_GetRenderWindow(renderer));
    extern int runSpeedHudChecks(World &,UiGameScreen &,ResourceManager *,SDL_Renderer *,const std::string &,bool &,
        const std::function<void(const char*)> &,const std::function<void(bool)> &,const std::function<float()> &);
    const int result=runSpeedHudChecks(*world,*g_hud,rm,renderer,outDir,g_widescreen,capture,
        [&](bool paused){setGamePaused(paused);},[]{syncGameSpeedControl();return g_gameSpeed;});
    if(timings)std::fclose(timings);
    return result;
  }
  if (std::getenv("ZT_SPEED_CHECKS")) {
    extern int runFastForwardChecks(ResourceManager *,SDL_Renderer *,const std::string &);
    const int result=runFastForwardChecks(rm,renderer,outDir);
    if(timings)std::fclose(timings);
    return result;
  }
  // ours: ZT_FLYER_SHOTS=1: the map's birds sent across and pictured where
  // they are, for looking at their height over the ground
  if (std::getenv("ZT_FLYER_SHOTS")) {
    Ambient &amb = world->getAmbient();
    amb.clear();
    for (int k = 0; k < 6; k++)
      amb.spawn(world->getMap(), -1);
    static bool noKeys[SDL_SCANCODE_COUNT] = {};
    setGamePaused(false);
    for (int s = 0; s < 40; s++)
      world->update(noKeys, 0.5f, 0.5f);
    setGamePaused(true);
    for (int i = 0; i < std::min(amb.count(), 4); i++) {
      int kind; float x, y, vx, vy;
      amb.flyer(i, kind, x, y, vx, vy);
      world->centreOn(x, y);
      SDL_Log("[Flyers] %d kind %d at %.1f,%.1f", i, kind, x, y);
      capture(("flyer-" + std::to_string(i)).c_str());
      // (and with the day/night cycle's sun shadows, mid-afternoon)
      const bool wasLight = Features::dayNightLighting;
      const double wasPreview = world->getLighting().previewHour;
      Features::dayNightLighting = true;
      world->getLighting().previewHour = 15.0;
      capture(("flyer-" + std::to_string(i) + "-lit").c_str());
      world->getLighting().previewHour = wasPreview;
      Features::dayNightLighting = wasLight;
    }
    if (timings) std::fclose(timings);
    return 0;
  }
  // ours: ZT_AUDIT_WORLD=1 (the real map): benches, picnic tables, play
  // equipment, stands, a restroom and the lake's shacks put by the path
  // from the entrance, guests let in with needs, the zoo run and each spot
  // pictured as it goes - for looking at what guests do there
  if (std::getenv("ZT_AUDIT_WORLD")) {
    static bool noKeys[SDL_SCANCODE_COUNT] = {};
    // (the entrance is found on the zoo's first update)
    setGamePaused(false);
    for (int s = 0; s < 5; s++)
      world->update(noKeys, 0.1f, 0.1f);
    WorldMap &map = world->getMap();
    Guests &gs = world->getGuests();
    int ex = -1, ey = -1;
    gs.entrance(ex, ey);
    std::vector<std::pair<int, int>> paths;
    if (ex >= 0) {
      std::set<std::pair<int, int>> seen{{ex, ey}};
      std::deque<std::pair<int, int>> open{{ex, ey}};
      while (!open.empty() && paths.size() < 400) {
        auto [x, y] = open.front();
        open.pop_front();
        paths.push_back({x, y});
        for (auto [dx, dy] : {std::pair{1, 0}, {-1, 0}, {0, 1}, {0, -1}})
          if (map.isPath(x + dx, y + dy) && seen.insert({x + dx, y + dy}).second)
            open.push_back({x + dx, y + dy});
      }
    }
    auto doorOnPath = [&](const PlacedObjects::Object &o) {
      auto doors = gs.doorTiles(o);
      if (doors.empty())
        return true;
      for (auto [dx, dy] : doors)
        if (map.isPath(dx, dy))
          return true;
      return false;
    };
    auto waterBy = [&](int x, int y) {
      for (int dy = -2; dy <= 2; dy++)
        for (int dx = -2; dx <= 2; dx++)
          if (const MapTile *t = map.getTile(x + dx, y + dy); t && (t->terrainType == 9 || t->terrainType == 10))
            return true;
      return false;
    };
    std::vector<std::pair<std::string, std::pair<float, float>>> placed;
    auto put = [&](const std::string &file, bool byWater, size_t from) {
      world->setObjectTool(file, 0, 0);
      for (size_t i = from; i < paths.size(); i++)
        for (int r = 1; r <= 2; r++)
          for (auto [dx, dy] : {std::pair{r, 0}, {-r, 0}, {0, r}, {0, -r}}) {
            const int tx = paths[i].first + dx, ty = paths[i].second + dy;
            if (map.isPath(tx, ty) || (byWater && !waterBy(tx, ty)))
              continue;
            for (float off : {0.5f, 1.0f})
              for (int facing : {4, 0, 2, 6}) {
                if (world->objectFit(file, tx + off, ty + off, facing) != Fences::Fit::Ok ||
                    !world->objectsMutable().add(file, tx + off, ty + off, facing))
                  continue;
                if (doorOnPath(world->getObjects().objects().back())) {
                  world->reindex();
                  world->setObjectTool("", 0, 0);
                  placed.push_back({file, {tx + off, ty + off}});
                  SDL_Log("[Audit] %s at %.1f,%.1f facing %d", file.c_str(), tx + off, ty + off, facing);
                  return true;
                }
                world->objectsMutable().remove(static_cast<int>(world->getObjects().objects().size()) - 1);
              }
          }
      world->setObjectTool("", 0, 0);
      SDL_Log("[Audit] no spot for %s", file.c_str());
      return false;
    };
    const std::vector<std::string> things = {
        "scenery/other/bench.ai",         "scenery/other/picnic.ai",     "scenery/other/sbench.ai",
        "scenery/building/swing.ai",      "scenery/building/dslide.ai",  "scenery/building/bounce.ai",
        "scenery/building/aqbounce.ai",   "scenery/building/bgrstnd.ai", "scenery/building/drkstnd.ai",
        "scenery/building/bathroom.ai",   "scenery/building/dinoputt.ai", "scenery/other/picnic2.ai",
        "scenery/other/aqbench.ai"};
    for (size_t k = 0; k < things.size(); k++)
      put(things[k], false, std::min(paths.size() ? paths.size() - 1 : 0, 6 + k * 4));
    for (const char *shack : {"scenery/building/boatshak.ai", "scenery/building/chroom.ai", "scenery/building/dolshack.ai"})
      put(shack, true, 0);
    // guests with needs, children among them
    for (int k = 0; k < 70; k++)
      gs.admit(0);
    int n = 0;
    for (const Guests::Guest &g : gs.guests()) {
      Guests::Guest &m = const_cast<Guests::Guest &>(g);
      m.tired = (n % 3 == 0) ? 85 : 20;
      m.hunger = (n % 4 == 1) ? 85 : 10;
      m.thirst = (n % 5 == 2) ? 85 : 10;
      m.bathroom = (n % 7 == 3) ? 90 : 0;
      n++;
    }
    for (int round = 0; round < 6; round++) {
      for (int s = 0; s < 400; s++)
        world->update(noKeys, 0.1f, 0.1f);
      for (size_t i = 0; i < placed.size(); i++) {
        world->centreOn(placed[i].second.first, placed[i].second.second);
        std::string name = placed[i].first.substr(placed[i].first.find_last_of('/') + 1);
        name = name.substr(0, name.size() - 3);
        capture(("audit-" + name + "-" + std::to_string(round)).c_str());
      }
      int using_ = 0, water = 0, walking = 0;
      for (const Guests::Guest &g : gs.guests()) {
        using_ += g.state == Guests::State::Using;
        water += g.state == Guests::State::Water;
        walking += g.state == Guests::State::Walking;
      }
      SDL_Log("[Audit] round %d: %zu guests, %d using, %d on water, %d walking", round, gs.guests().size(), using_,
              water, walking);
    }
    setGamePaused(true);
    if (timings) std::fclose(timings);
    return 0;
  }
  // ours: ZT_MAUL_SAVE=<file.zoo> (the real map): guests let in and walking
  // the paths, every member of staff let go, and a black bear put loose by
  // the busiest path - saved shared, for the original to play (a loose
  // predator mauling a guest, compared with ours)
  if (const char *maulSave = std::getenv("ZT_MAUL_SAVE")) {
    static bool noKeys[SDL_SCANCODE_COUNT] = {};
    setGamePaused(false);
    for (int s = 0; s < 5; s++)
      world->update(noKeys, 0.1f, 0.1f);
    Guests &gs = world->getGuests();
    for (int k = 0; k < 60; k++)
      gs.admit(0);
    auto inZoo = [&gs] {
      int n = 0;
      for (const Guests::Guest &g : gs.guests())
        n += g.x > -999;
      return n;
    };
    SDL_Log("[Maul] %d guests let in", inZoo());
    for (int s = 0; s < 300; s++)
      world->update(noKeys, 0.1f, 0.1f);
    SDL_Log("[Maul] %d guests after 30 s", inZoo());
    std::vector<int> staffIds;
    for (const Staff::Member &m : world->getStaff().members())
      staffIds.push_back(m.id);
    for (int id : staffIds)
      world->getStaff().fire(id);
    WorldMap &map = world->getMap();
    Fences &fz = world->getFences();
    int bx = -1, by = -1, most = 0;
    for (const Guests::Guest &g : gs.guests()) {
      if (g.x < -999)
        continue;
      const int gx = static_cast<int>(std::floor(g.x)), gy = static_cast<int>(std::floor(g.y));
      for (auto [dx, dy] : {std::pair{2, 0}, {-2, 0}, {0, 2}, {0, -2}}) {
        const int tx = gx + dx, ty = gy + dy;
        if (map.isPath(tx, ty) || !fz.insideZoo(tx, ty) || fz.exhibitAt(tx, ty) >= 0)
          continue;
        int near = 0;
        for (const Guests::Guest &o : gs.guests())
          near += o.x > -999 && std::hypot(o.x - (tx + 0.5f), o.y - (ty + 0.5f)) < 4.0f;
        if (near > most) {
          most = near;
          bx = tx;
          by = ty;
        }
      }
    }
    Animals &an = world->getAnimals();
    const int bearType = an.typeOfFile("animals/blackbr.ai");
    // (put down outside any exhibit: as a loose one is, not as a player
    // places one)
    const int bear = bx >= 0 && bearType >= 0 ? an.adopt(bearType, false, bx + 0.5f, by + 0.5f, map, fz, true) : -1;
    for (int s = 0; s < 3; s++)
      world->update(noKeys, 0.1f, 0.1f);
    if (Animals::Member *b = an.member(bear))
      world->centreOn(b->x, b->y);
    int walking = 0;
    for (const Guests::Guest &g : gs.guests())
      walking += g.x > -999;
    SDL_Log("[Maul] bear %d at %d,%d with %d guests near, %d guests in the zoo, %zu staff let go; saved %s: %d", bear, bx,
            by, most, walking, staffIds.size(), maulSave,
            static_cast<int>(g_hud && SaveGame::save(maulSave, *world, g_sim, *g_hud, g_currentMapPath)));
    capture("maul_save");
    setGamePaused(true);
    if (timings) std::fclose(timings);
    return 0;
  }
  // ours: ZT_LIVE_SCENE=<script>: a test zoo built from a script, run,
  // pictured and saved shared, to play the same scene in the original
  // (src/LiveScene.cpp)
  if (const char *scene = std::getenv("ZT_LIVE_SCENE")) {
    extern int runLiveScene(World &, ZooSim &, const std::string &, const std::function<void(const char *)> &,
                            const std::function<bool(const std::string &)> &, const std::function<void(float)> &);
    // (ZT_LIVE_LOAD=<file>: a saved zoo loaded first, to run on from it as
    // the original does from the same file)
    if (const char *load = std::getenv("ZT_LIVE_LOAD")) {
      const bool ok = gameLoad(rm, load);
      world = g_world;
      note((std::string("loaded save ") + load + (ok ? " ok" : " FAILED")).c_str());
      if (!ok)
        return 1;
    }
    setGamePaused(false);
    static bool sceneKeys[SDL_SCANCODE_COUNT] = {};
    const int result = runLiveScene(
        *world, g_sim, scene, capture,
        [&](const std::string &file) { return g_hud && SaveGame::save(file, *world, g_sim, *g_hud, g_currentMapPath); },
        [&](float dt) {
          // (as the game's own frame: the world, the books and calendar, and
          // the money and news passed on)
          world->objectsMutable().today = g_sim.today();
          world->getFences().today = g_sim.today();
          world->update(sceneKeys, dt, dt);
          g_sim.update(dt);
          worldInputs(renderer, rm, std::vector<Input>());
        });
    setGamePaused(true);
    if (timings) std::fclose(timings);
    return result;
  }
  if (std::getenv("ZT_HUD_CLOCK_CHECKS")) {
    SDL_HideWindow(SDL_GetRenderWindow(renderer));
    extern int runHudClockChecks(World &,UiGameScreen &,ResourceManager *,SDL_Renderer *,const std::string &,
                                bool &,const std::function<void(const char*)> &,const std::function<void(bool)> &);
    // ours: 17 January, so the celebrations date is at its widest (a
    // two-digit day)
    for (int d = 0; d < 16; d++)
      g_sim.update(ZooSim::kSecondsPerDay);
    const int result = runHudClockChecks(*world,*g_hud,rm,renderer,outDir,g_widescreen,capture,
                                        [&](bool paused) { setGamePaused(paused); });
    if (timings) std::fclose(timings);
    return result;
  }
  // ZT_EVERYTHING_ZOO=1: every type in the registry placed, saved shared,
  // and its original part checked (src/EverythingZoo.cpp)
  // ZT_SHOW_ALL=1: every show species, trick, toy and schedule
  // (src/ShowAllChecks.cpp)
  // ZT_FINANCE_CHECKS=1: funding paid only when affordable, purchases
  // refused without the money (src/FinanceChecks.cpp)
  // ZT_CELEBRATION_CHECKS=1 (with ZT_FLAT_LAB=1): the themed celebrations
  // (Features::celebrations; src/CelebrationChecks.cpp), its report in
  // <out>/celebration-checks.txt. Its year: a saved zoo played a year with
  // the switch off, then on, the books, rating, goals and guests compared
  if (std::getenv("ZT_CELEBRATION_CHECKS")) {
    extern int runCelebrationChecks(World &, UiGameScreen &, ZooSim &, ResourceManager *, const std::string &,
                                    const std::function<std::string(bool)> &);
    const std::string baseline = outDir + "/celebration-baseline.zoo";
    {
      Guests &seed = world->getGuests();
      seed.clear();
      seed.setEntrance(25, 26);
      for (int k = 0; k < 6; k++)
        seed.admit(k % 4);
      world->objectsMutable().add("scenery/building/aqgftshp.ai", 14.0f, 58.0f, 0); // (a colour-replaced building)
    }
    const bool saved = gameSave(baseline);
    auto year = [&](bool on) -> std::string {
      if (!saved || !gameLoad(rm, baseline))
        return "";
      world = g_world;
      Features::celebrations = on;
      Celebrations::reset();
      setGamePaused(false);
      std::srand(7);
      static bool noKeys[SDL_SCANCODE_COUNT] = {};
      const float dt = 0.5f;
      unsigned long long trace = 1469598103934665603ull; // (the zoo's state every minute, folded together)
      int maxGuests = 0, maxBuildings = 0;
      for (int step = 0; step < static_cast<int>(365 * ZooSim::kSecondsPerDay / dt); step++) {
        g_world->objectsMutable().today = g_sim.today();
        g_world->getFences().today = g_sim.today();
        g_world->update(noKeys, dt, dt);
        g_sim.update(dt);
        g_goals.update(dt);
        g_world->setDate(g_sim.dayOfMonth(), g_sim.month(), g_sim.year());
        Celebrations::update(*g_world, g_sim.dayOfMonth(), g_sim.month(), g_sim.year(),
                             [](const std::string &line) { g_hud->postMessage(line, 0); });
        maxGuests = std::max(maxGuests, Celebrations::recolouredGuests());
        maxBuildings = std::max(maxBuildings, Celebrations::recolouredBuildings());
        if (step % 120 == 0) {
          std::ostringstream now;
          double happy = 0;
          for (const Guests::Guest &g : g_world->getGuests().mutableGuests())
            happy += g.happiness + g.hunger + g.thirst;
          now << std::fixed << std::setprecision(3) << g_sim.cash() << ' ' << g_sim.rating() << ' ' << happy << ' '
              << g_world->getGuests().mutableGuests().size();
          trace = (trace ^ std::hash<std::string>{}(now.str())) * 1099511628211ull;
        }
      }
      std::ostringstream digest;
      digest << std::fixed << std::setprecision(2) << g_sim.cash() << " rating " << g_sim.rating() << " date "
             << g_sim.dayOfMonth() << "/" << g_sim.month() << "/" << g_sim.year() << " guests "
             << g_world->getGuests().mutableGuests().size() << " animals " << g_world->getAnimals().members().size()
             << " books";
      for (const ZooSim::Month &m : g_sim.history()) {
        digest << " [" << m.month << "/" << m.year << " " << m.total() << " " << m.rating << " " << m.guests << "]";
      }
      digest << " goals";
      for (const Goals::Goal &goal : g_goals.all())
        digest << " " << goal.name << ":" << goal.state;
      digest << " trace " << trace << " buildings";
      for (const PlacedObjects::Object &o : g_world->getObjects().objects())
        if (o.subClass == "building") {
          digest << " [";
          for (int c : o.colours)
            digest << c << ",";
          digest << "]";
        }
      // (after the '#': what the celebrations did, not compared)
      digest << "#flyers sent " << g_world->getAmbient().sent() << " most guests recoloured " << maxGuests
             << " most buildings recoloured " << maxBuildings;
      return digest.str();
    };
    setGamePaused(true);
    // (a picture of the zoo's birthday fireworks, by day and by night: look only)
    {
      const bool wasCel = Features::celebrations, wasLight = Features::dayNightLighting;
      Features::celebrations = true;
      Celebrations::reset();
      Celebrations::update(*world, 1, 0, 2, nullptr);
      Celebrations::tickFireworks(0.9f);
      capture("celebration-fireworks-day");
      Features::dayNightLighting = true;
      const double wasPreview = world->getLighting().previewHour;
      world->getLighting().previewHour = 21.0;
      Celebrations::tickFireworks(0.4f);
      capture("celebration-fireworks-night");
      world->getLighting().previewHour = wasPreview;
      Features::dayNightLighting = wasLight;
      Features::celebrations = wasCel;
      Celebrations::reset();
      world->getAmbient().clear();
    }
    const int result = runCelebrationChecks(*world, *g_hud, g_sim, rm, outDir, year);
    Features::celebrations = false;
    if (timings) std::fclose(timings);
    return result;
  }
  // ZT_ACTIVITY_CHECKS=1: the Lab's "activitycheck" fixture (tank clipping,
  // marine activity), its report in <out>/activity-checks.txt
  if (std::getenv("ZT_ACTIVITY_CHECKS")) {
    extern std::string runActivityPolishChecks(World &, ResourceManager *, bool);
    extern std::string runCameraRenderChecks(SDL_Renderer *);
    const std::string result = runActivityPolishChecks(*world, rm, false) + runCameraRenderChecks(renderer) + "\n";
    std::ofstream(outDir + "/activity-checks.txt") << result;
    if(timings)std::fclose(timings);return result.find("FAIL ") == std::string::npos ? 0 : 1;
  }
  // ZT_PARITY_CHECKS=1 (with ZT_FLAT_LAB=1): the Lab's "paritycheck"
  // fixture (src/ActivityPolishChecks.cpp runParityChecks), its report in
  // <out>/parity-checks.txt
  if (std::getenv("ZT_PARITY_CHECKS")) {
    extern std::string runParityChecks(World &, ResourceManager *);
    setRenameHooks(rm); // (as a game started has them)
    const std::string result = runParityChecks(*world, rm);
    std::ofstream(outDir + "/parity-checks.txt") << result;
    if(timings)std::fclose(timings);return result.find("FAIL ") == std::string::npos && result.find("PASS ") != std::string::npos ? 0 : 1;
  }
  // ZT_MODMENU_CHECK=1: the Mod Menu as the player's mods folder fills it -
  // its rows, a picture, and the first mod switched on through it (then put
  // back as it was)
  if (std::getenv("ZT_MODMENU_CHECK")) {
    std::ofstream report(outDir + "/modmenu-checks.txt");
    UiLayout *layout = ModMenu::open(rm, getLayoutPath(rm, "scenario.lyt"));
    UiListBox *list = layout ? dynamic_cast<UiListBox *>(layout->getElementById(50002)) : nullptr;
    const auto mods = Mods::list();
    report << "mods in the folder: " << mods.size() << "\n";
    for (const auto &m : mods)
      report << "     " << m.folder << " (" << m.name << ") " << (m.enabled ? "on" : "off") << "\n";
    report << "rows in the list: " << (list ? static_cast<int>(list->getItemCount()) : -1) << "\n";
    auto shot = [&](const char *name) {
      SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
      SDL_RenderClear(renderer);
      drawLayoutCentered(renderer, layout, rm);
      int w = 0, h = 0;
      SDL_GetCurrentRenderOutputSize(renderer, &w, &h);
      if (SDL_Surface *s = SDL_CreateSurface(w, h, SDL_PIXELFORMAT_ARGB8888)) {
        if (ZT_RenderReadPixels(renderer, nullptr, SDL_PIXELFORMAT_ARGB8888, s->pixels, s->pitch) == 0)
          SDL_SaveBMP(s, (outDir + "/" + name + ".bmp").c_str());
        SDL_DestroySurface(s);
      }
      SDL_RenderPresent(renderer);
    };
    shot("modmenu_list");
    if (list && !mods.empty()) {
      const bool was = mods[0].enabled;
      list->setSelectedIndex(1); // (the first mod: the cheat's row is above it)
      ModMenu::selectionChanged(layout, rm);
      ModMenu::toggle(layout, rm);
      bool now = false;
      for (const auto &m : Mods::list())
        if (m.folder == mods[0].folder)
          now = m.enabled;
      report << (now != was ? "PASS" : "FAIL") << " the menu switches " << mods[0].folder << " " << (was ? "off" : "on")
             << "\n";
      shot("modmenu_toggled");
      ModMenu::toggle(layout, rm); // (back as it was)
      for (const auto &m : Mods::list())
        if (m.folder == mods[0].folder)
          report << "     put back: " << (m.enabled ? "on" : "off") << "\n";
    }
    // (the sizes, worked out in the background, then at the rows' right)
    for (int i = 0; i < 100; i++) {
      ModMenu::tick(layout, rm);
      SDL_Delay(50);
    }
    shot("modmenu_sizes");
    // (a mod with its own picture, mod.json "picture", picked: it shows)
    for (const auto &m : mods)
      if (!m.picture.empty() && list) {
        for (int i = 0; i < static_cast<int>(list->getItemCount()); i++) {
          list->setSelectedIndex(i);
          if (list->getSelectedData() == m.folder)
            break;
        }
        ModMenu::selectionChanged(layout, rm);
        report << "     picture of " << m.folder << ": " << m.picture << "\n";
        shot("modmenu_picture");
        break;
      }
    // The repository's view, where the installed game has its client: each
    // part, and whether it's counted as in your mods (downloaded, or the
    // same mod's files there already)
    if (ModRepo::available()) {
      ModMenu::repository(layout, rm);
      for (int i = 0; i < 400; i++) {
        ModMenu::tick(layout, rm);
        const ModRepo::State st = ModRepo::state();
        if (st == ModRepo::State::Ready || st == ModRepo::State::Failed)
          break;
        SDL_Delay(50);
      }
      ModMenu::tick(layout, rm);
      report << "     repository: " << ModRepo::status() << "\n";
      for (const ModRepo::Mod &m : ModRepo::mods())
        for (const ModRepo::Part &p : m.parts) {
          const std::string v = ModRepo::installedVersion(m, p);
          report << "     repo " << m.id << "/" << p.id << ": " << (v.empty() ? "not in your mods" : "in your mods, " + v)
                 << "\n";
        }
      shot("modmenu_repo");
      ModMenu::repository(layout, rm);
    } else {
      report << "     (no repository client in this copy)\n";
    }
    delete layout;
    if(timings)std::fclose(timings);return 0;
  }
  if (std::getenv("ZT_FINANCE_CHECKS")) {
    // The emergency funding drive (freeform/donation.scn lowmoney_donate,
    // once): under $1,000 the goals' next check gives $10,000 and its popup
    {
      std::ofstream goalsReport(outDir + "/finance-goals.txt");
      // (the map's goals, as a game started from the menu has them)
      if (g_goals.all().empty())
        startGoals(rm, g_currentMapPath);
      goalsReport << "     map " << g_currentMapPath << ", " << g_goals.all().size() << " goals\n";
      for (const Goals::Goal &goal : g_goals.all())
        if (goal.name.rfind("lowmoney", 0) == 0)
          goalsReport << "     " << goal.name << " state " << goal.state << " rule " << goal.rulea << "/" << goal.ruleb
                      << " type " << goal.type << " value " << goal.value << "\n";
      // (shared saves for the original to judge: before the grant, and after)
      g_sim.setCash(20000);
      gameSave(outDir + "/goals-control.zoo");
      const double before = 500;
      g_sim.setCash(before);
      g_goals.evaluateAll();
      const bool popup = g_hud->hasDialog();
      const double after = g_sim.cash();
      capture("finance_lowmoney_popup");
      goalsReport << (after - before == 10000 && popup ? "PASS" : "FAIL") << " under $1,000: $"
                  << std::lround(after - before) << " given, popup " << (popup ? "shown" : "not shown") << "\n";
      {
        // (read and dismissed, as a player would: Enter)
        std::vector<Input> enterKey(1);
        enterKey[0].type = InputType::BUTTON;
        enterKey[0].event = InputEvent::KEY_DOWN;
        enterKey[0].key = SDLK_RETURN;
        g_hud->handleInputs(enterKey);
      }
      g_sim.setCash(500);
      g_goals.evaluateAll();
      goalsReport << (g_sim.cash() == 500 ? "PASS" : "FAIL") << " only once: a second time under $1,000 gives $"
                  << std::lround(g_sim.cash() - 500) << "\n";
      // The goals' states in an original zoo (#68): saved in the original's
      // order, and an original zoo's states put back, so the grant isn't
      // given again in either game
      {
        const std::string script = g_goals.script();
        const auto slots = Goals::originalOrder(rm, script);
        int slot = -1;
        for (size_t i = 0; i < slots.size(); i++)
          if (slots[i].name == "lowmoney_donate" && slot < 0)
            slot = static_cast<int>(i);
        int donateState = 0;
        for (const Goals::Goal &goal : g_goals.all())
          if (goal.name == "lowmoney_donate")
            donateState = goal.state;
        const std::string file = outDir + "/goals-original.zoo";
        std::string error;
        g_sim.setCash(20000);
        gameSave(outDir + "/goals-granted.zoo");
        const bool saved = SaveGame::saveOriginal(file, *g_world, g_sim, *g_hud, error);
        goalsReport << "     script " << script << ": " << slots.size() << " states, lowmoney_donate at " << slot
                    << " state " << donateState << (saved ? "" : ", not saved: " + error) << "\n";
        if (saved && gameLoad(rm, file)) {
          world = g_world;
          const ZooReader &zr = g_world->mapReader();
          const auto &st = zr.getGoalStates();
          const int written = slot >= 0 && slot < static_cast<int>(st.size()) ? st[slot] : -1;
          // (before version 86, up to the first composite: 0x5950fd)
          size_t held = slots.size();
          if (zr.getVersion() <= 0x55)
            for (size_t i = 0; i < slots.size(); i++)
              if (slots[i].composite) {
                held = i;
                break;
              }
          goalsReport << (zr.getScenarioName() == script && st.size() == held && written == donateState &&
                                  donateState != 0
                              ? "PASS"
                              : "FAIL")
                      << " the original zoo's scenario block (version " << zr.getVersion() << "): '"
                      << zr.getScenarioName() << "', " << st.size() << " of " << slots.size()
                      << " states, lowmoney_donate " << written << "\n";
          int loaded = -1;
          for (const Goals::Goal &goal : g_goals.all())
            if (goal.name == "lowmoney_donate")
              loaded = goal.state;
          g_sim.setCash(500);
          g_goals.evaluateAll();
          goalsReport << (g_goals.script() == script && loaded == donateState && g_sim.cash() == 500 ? "PASS" : "FAIL")
                      << " loaded back as an original zoo: goals from " << g_goals.script() << ", lowmoney_donate "
                      << loaded << ", under $1,000 gives $" << std::lround(g_sim.cash() - 500) << "\n";
        } else {
          goalsReport << "FAIL the original zoo saved and loaded back\n";
        }
      }
      // The advisor (zooAdvisories 0x420347): its shares and the 10-guest
      // floor, at their edges, and the census of real guests
      {
        auto has = [](const std::vector<std::pair<int, int>> &v, int id) {
          for (auto [i, k] : v)
            if (i == id)
              return true;
          return false;
        };
        Guests::Census c;
        c.guests = 20;
        c.hungry = 5; // (25%: over cPctHungry's 20%)
        goalsReport << (has(zooAdvice(c, 0, 0), 10011) ? "PASS" : "FAIL") << " advice: 5 hungry guests in 20 (25%)\n";
        c.hungry = 4; // (20%: not over)
        goalsReport << (!has(zooAdvice(c, 0, 0), 10011) ? "PASS" : "FAIL") << " advice: none for 4 in 20 (20%)\n";
        c.guests = 10;
        c.hungry = 10;
        goalsReport << (!has(zooAdvice(c, 0, 0), 10011) ? "PASS" : "FAIL") << " advice: none with only 10 guests\n";
        goalsReport << (has(zooAdvice(c, 10, 4), 10009) && !has(zooAdvice(c, 10, 4), 10008) ? "PASS" : "FAIL")
                    << " advice: protestors at 4 sick in 10 animals, whatever the guests; sick animals only with more than 10\n";
        c.guests = 11;
        c.hungry = 0;
        c.happiness = 80;
        goalsReport << (has(zooAdvice(c, 0, 0), 10021) ? "PASS" : "FAIL") << " advice: guests very happy at 80\n";
        c.happiness = -20;
        goalsReport << (has(zooAdvice(c, 0, 0), 10022) ? "PASS" : "FAIL") << " advice: guests unhappy at -20\n";
        c.gift = 8; // (73%: over cPctSouvenir's 70%)
        c.trash = 5; // (45%: over cPctTrash's 40%)
        auto v = zooAdvice(c, 0, 0);
        goalsReport << (has(v, 10014) && has(v, 10016) ? "PASS" : "FAIL") << " advice: gifts wanted, litter in hand\n";
        // (the census: guests over cUIThreshold counted, the rest not)
        Guests &gs = world->getGuests();
        gs.clear();
        if (!gs.hasEntrance())
          gs.setEntrance(40, 40); // (the flat test map has none)
        for (int n = 0; n < 12; n++) {
          const size_t had = gs.guests().size();
          gs.admit(0);
          if (gs.guests().size() == had)
            break;
          Guests::Guest &g = const_cast<Guests::Guest &>(gs.guests().back());
          g.x = 40.5f;
          g.y = 40.5f;
          g.hunger = n < 3 ? 80.0f : 10.0f;
          g.thirst = n < 4 ? 76.0f : 75.0f;
          g.happiness = 50;
        }
        const Guests::Census real = gs.census();
        goalsReport << (real.guests == 12 && real.hungry == 3 && real.thirsty == 4 && real.happiness == 50 ? "PASS" : "FAIL")
                    << " census: 12 guests, 3 over 75 hungry, 4 over 75 thirsty (75 itself not), happiness 50 ("
                    << real.guests << ", " << real.hungry << ", " << real.thirsty << ", " << real.happiness << ")\n";
        gs.clear();
      }
      // (the refusal's message, wrapped in the bar as the original's)
      g_hud->showMessage(rm->getString(10002));
      capture("finance_message_bar");
    }
    setupGoals(rm);
    {extern Goals::Hooks g_financeGoalHooks;g_financeGoalHooks=g_goals.getHooks();}
    int result=runFinanceChecks(*world,*g_hud,g_sim,rm,outDir);
    // A loaded zoo rated at once, even paused (zoo.exe worldAfterLoad
    // 0x4534e6 -> zooRatingTerms 0x41fcc1; #256): saved at a rating of 99,
    // the first frame after the load works it out again (last: it loads a
    // zoo of its own)
    {
      std::ofstream ratingReport(outDir + "/finance-rating.txt");
      g_sim.setRating(99);
      const std::string file = outDir + "/rating-after-load.zoo";
      const bool saved = gameSave(file);
      bool ok = false;
      if (saved && gameLoad(rm, file)) {
        world = g_world;
        setGamePaused(true);
        const double loaded = g_sim.rating();
        std::vector<Input> none;
        worldInputs(renderer, rm, none);
        const double rated = g_sim.rating();
        setGamePaused(false);
        ok = loaded == 99 && rated != 99 && !g_rateNow;
        ratingReport << (ok ? "PASS" : "FAIL") << " a loaded zoo rated at once, paused (0x4534e6): saved 99, loaded "
                     << loaded << ", after one frame " << rated << "\n";
      } else {
        ratingReport << "FAIL the rating-after-load zoo saved and loaded back\n";
      }
      SDL_Log("[Finance] %s a loaded zoo rated at once, paused (0x4534e6, #256)", ok ? "PASS" : "FAIL");
      if (!ok)
        result = 1;
    }
    // Terraforming settled as the original's (zoo.exe 0x4da2e0: leaving the
    // tab, the price 0x4d9c0f paid for if the zoo can, else put back and
    // 3363 in the OK dialog): a tile painted with a terrain that costs
    {
      std::ofstream terraReport(outDir + "/finance-terraform.txt");
      TerrainTool &tt = g_world->getTerrainTool();
      WorldMap &map = g_world->getMap();
      int type = -1, tx = -1, ty = -1, before = -1;
      float price = 0;
      for (int t = 0; t < 64 && type < 0; ++t) {
        if (tt.typeCost(t) <= 0)
          continue;
        for (int y = 0; y < map.getHeight() && type < 0; ++y)
          for (int x = 0; x < map.getWidth() && type < 0; ++x) {
            if (!g_world->getFences().insideZoo(x, y))
              continue;
            const MapTile *tile = map.getTile(x, y);
            if (!tile || tile->terrainType == t)
              continue;
            const int was = tile->terrainType;
            g_world->setTerrainTool(true, true, t, 1, 0);
            tt.press(x, y, 0, 0);
            tt.release();
            if (tt.cost() > 0 && !tt.isWater(x, y)) {
              type = t; tx = x; ty = y; before = was; price = tt.cost();
            } else {
              tt.undo();
              g_world->setTerrainTool(false, true, t, 1, 0);
            }
          }
      }
      {
        int costly = 0, inside = 0;
        for (int t = 0; t < 64; ++t) costly += tt.typeCost(t) > 0;
        for (int y = 0; y < map.getHeight(); ++y)
          for (int x = 0; x < map.getWidth(); ++x) inside += g_world->getFences().insideZoo(x, y);
        terraReport << "     terrains with a cost " << costly << ", tiles inside the zoo " << inside << " of "
                    << map.getWidth() * map.getHeight() << "\n";
      }
      bool ok = type >= 0 && price == tt.typeCost(type);
      terraReport << (ok ? "PASS" : "FAIL") << " a tile painted is priced at its terrain's cost (0x4d9c0f): type " << type
                  << " at " << tx << "," << ty << " $" << price << ", cost $" << (type >= 0 ? tt.typeCost(type) : 0) << "\n";
      bool refused = false, paid = false;
      if (type >= 0) {
        // (a dollar short: put back, the dialog, no money taken)
        g_sim.setCash(price - 1);
        settleTerrain(rm);
        const MapTile *tile = map.getTile(tx, ty);
        refused = tile && tile->terrainType == before && g_hud->hasDialog() && g_sim.cash() == price - 1 && tt.cost() == 0;
        terraReport << (refused ? "PASS" : "FAIL") << " a dollar short: put back, 3363 in the OK dialog (showDialog 0xd23), nothing paid: terrain "
                    << (tile ? int(tile->terrainType) : -1) << " (was " << before << "), dialog " << g_hud->hasDialog()
                    << ", cash $" << g_sim.cash() << "\n";
        std::vector<Input> enterKey(1);
        enterKey[0].type = InputType::BUTTON;
        enterKey[0].event = InputEvent::KEY_DOWN;
        enterKey[0].key = SDLK_RETURN;
        g_hud->handleInputs(enterKey);
        // (enough: kept and paid for, on the Construction line)
        tt.press(tx, ty, 0, 0);
        tt.release();
        g_sim.setCash(price + 100);
        const double built = g_sim.history().empty() ? 0 : g_sim.history().back().lines[ZooSim::Construction];
        settleTerrain(rm);
        const double builtAfter = g_sim.history().empty() ? 0 : g_sim.history().back().lines[ZooSim::Construction];
        tile = map.getTile(tx, ty);
        paid = tile && tile->terrainType == type && g_sim.cash() == 100 && builtAfter - built == -price && !g_hud->hasDialog();
        terraReport << (paid ? "PASS" : "FAIL") << " enough: kept and paid for on the Construction line: terrain "
                    << (tile ? int(tile->terrainType) : -1) << ", cash $" << g_sim.cash() << ", Construction "
                    << (builtAfter - built) << "\n";
        if (MapTile *t = map.getTileMutable(tx, ty))
          t->terrainType = static_cast<uint8_t>(before);
        map.touch();
      }
      g_world->setTerrainTool(false, true, 0, 1, 0);
      ok = ok && refused && paid;
      SDL_Log("[Finance] %s terraforming paid for, or put back with the dialog (0x4da2e0, 0x4d9c0f)", ok ? "PASS" : "FAIL");
      if (!ok)
        result = 1;
    }
    if(timings)std::fclose(timings);return result;
  }
  if (std::getenv("ZT_LIGHTING_GALLERY")) {
    // ours: every building's shadows and night lights, to look over (LightingGallery.cpp)
    extern int runLightingGallery(World &,ResourceManager *,SDL_Renderer *,const std::string &);
    const int result=runLightingGallery(*world,rm,renderer,outDir);
    if(timings)std::fclose(timings);return result;
  }
  if (std::getenv("ZT_LIGHTING_BENCH")) {
    extern int runLightingBenchmarks(World &,ResourceManager *,SDL_Renderer *,const std::string &,const std::function<void()> &);
    const int result=runLightingBenchmarks(*world,rm,renderer,outDir,[&]{
      world->setView(gameViewRect(renderer),getUiTransform(renderer).scale);world->draw(renderer);drawHud(renderer,rm);
    });
    if(timings)std::fclose(timings);return result;
  }
  if (std::getenv("ZT_LIGHTING_CHECKS")) {
    extern int runLightingChecks(World &,ResourceManager *,SDL_Renderer *,const std::string &,const std::function<void(const char*)> &,const SDL_Rect &);
    const int result=runLightingChecks(*world,rm,renderer,outDir,capture,gameViewRect(renderer));
    Features::dayNightLighting=true;world->getLighting().previewHour=0;
    g_hud->options->open(true,2);capture("lighting_graphics_option");
    g_hud->options->close();world->getLighting().previewHour=-1;Features::dayNightLighting=false;
    if(!result && std::getenv("ZT_LIGHTING_VIDEO")) {
      extern int runLightingVideo(World &,ResourceManager *,SDL_Renderer *,const std::function<void()> &,const std::function<void(bool)> &);
      auto showOptions=[&](bool open) {
        if(open)g_hud->options->open(true,2);
        else { g_hud->options->close();std::vector<Input> none;g_hud->options->handleInputs(none); }
      };
      const int video=runLightingVideo(*world,rm,renderer,[&] {
        world->setView(gameViewRect(renderer),getUiTransform(renderer).scale);world->draw(renderer);drawHud(renderer,rm);
        if(g_hud->optionsOpen())drawOptions(renderer,rm,g_hud->options);
        drawDialogOverOptions(renderer, rm);
      },showOptions);
      if(timings)std::fclose(timings);return video;
    }
    if(!result && std::getenv("ZT_SHADOW_CHECKS")) {
      std::vector<Input> noInput;g_hud->options->handleInputs(noInput);
      extern int runLightingShadowChecks(World &,ResourceManager *,SDL_Renderer *,const std::string &,const std::function<void(const char*)> &);
      const int shadow=runLightingShadowChecks(*world,rm,renderer,outDir,capture);
      if(timings)std::fclose(timings);return shadow;
    }
    if(timings)std::fclose(timings);return result;
  }
  if (std::getenv("ZT_SHOW_ALL")) {
    int result=runShowAllChecks(*world,*g_hud,g_sim,rm,outDir);
    if(timings)std::fclose(timings);return result;
  }
  // ours: ZT_TOUR_ZOO=<file.zoo>: the tour guides' side by side zoo (#129),
  // src/ActivityPolishChecks.cpp buildTourZoo
  if (const char *tourZoo = std::getenv("ZT_TOUR_ZOO")) {
    extern std::string buildTourZoo(World &, ZooSim &, UiGameScreen &, ResourceManager *, const std::string &, const std::string &);
    const std::string r = buildTourZoo(*world, g_sim, *g_hud, rm, g_currentMapPath, tourZoo);
    SDL_Log("[TourZoo] %s", r.c_str());
    if(timings)std::fclose(timings);return r.rfind("ok", 0) == 0 ? 0 : 1;
  }
  // ours: ZT_SHOW_ZOO=<file.zoo>: a show to watch in the original (#301)
  if (const char *showZoo = std::getenv("ZT_SHOW_ZOO")) {
    extern std::string buildShowZoo(World &, ZooSim &, UiGameScreen &, const std::string &, const std::string &);
    const std::string r = buildShowZoo(*world, g_sim, *g_hud, g_currentMapPath, showZoo);
    SDL_Log("[ShowZoo] %s", r.c_str());
    if(timings)std::fclose(timings);return r.rfind("ok", 0) == 0 ? 0 : 1;
  }
  // ours: ZT_JOIN_ZOO=<file.zoo>: two joined tanks and an unassigned
  // specialist, for measuring a keeper's hop to a joined habitat (#297);
  // with ZT_JOIN_SIM=1 it runs 300 s of it and checks the specialist's
  // visits (show tank, then home tank, no idle between)
  if (const char *joinZoo = std::getenv("ZT_JOIN_ZOO")) {
    extern std::string buildJoinedTankZoo(World &, ZooSim &, UiGameScreen &, const std::string &, const std::string &);
    const std::string r = buildJoinedTankZoo(*world, g_sim, *g_hud, g_currentMapPath, joinZoo);
    SDL_Log("[JoinZoo] %s", r.c_str());
    if(timings)std::fclose(timings);return r.rfind("ok", 0) == 0 ? 0 : 1;
  }
  // ours: ZT_WALK_ZOO=<file.zoo>: two paddocks and their keepers, for
  // measuring walking about in the original (#288)
  if (const char *walkZoo = std::getenv("ZT_WALK_ZOO")) {
    extern std::string buildWalkZoo(World &, ZooSim &, UiGameScreen &, const std::string &, const std::string &);
    const std::string r = buildWalkZoo(*world, g_sim, *g_hud, g_currentMapPath, walkZoo);
    SDL_Log("[WalkZoo] %s", r.c_str());
    if(timings)std::fclose(timings);return r.rfind("ok", 0) == 0 ? 0 : 1;
  }
  // ours: ZT_ACCESS_ZOO=<file.zoo>: exhibits with their gates reachable or
  // not, for 11000 / 11001 in the original (#145)
  if (const char *accessZoo = std::getenv("ZT_ACCESS_ZOO")) {
    extern std::string buildAccessZoo(World &, ZooSim &, UiGameScreen &, const std::string &, const std::string &);
    const std::string r = buildAccessZoo(*world, g_sim, *g_hud, g_currentMapPath, accessZoo);
    SDL_Log("[AccessZoo] %s", r.c_str());
    if(timings)std::fclose(timings);return r.rfind("ok", 0) == 0 ? 0 : 1;
  }
  // ours: ZT_LANDJOIN_ZOO=<file.zoo>: a pool between two lands with a walrus
  // in it, for measuring which land the original gives it (#316)
  if (const char *landJoinZoo = std::getenv("ZT_LANDJOIN_ZOO")) {
    extern std::string buildLandJoinZoo(World &, ZooSim &, UiGameScreen &, const std::string &, const std::string &);
    const std::string r = buildLandJoinZoo(*world, g_sim, *g_hud, g_currentMapPath, landJoinZoo);
    SDL_Log("[LandJoinZoo] %s", r.c_str());
    if(timings)std::fclose(timings);return r.rfind("ok", 0) == 0 ? 0 : 1;
  }
  if (const char *amphibZoo = std::getenv("ZT_AMPHIB_ZOO")) {
    extern std::string buildAmphibZoo(World &, ZooSim &, UiGameScreen &, const std::string &, const std::string &);
    const std::string r = buildAmphibZoo(*world, g_sim, *g_hud, g_currentMapPath, amphibZoo);
    SDL_Log("[AmphibZoo] %s", r.c_str());
    if(timings)std::fclose(timings);return r.rfind("ok", 0) == 0 ? 0 : 1;
  }
  if (const char *escapeZoo = std::getenv("ZT_ESCAPE_ZOO")) {
    extern std::string buildEscapeZoo(World &, ZooSim &, UiGameScreen &, const std::string &, const std::string &);
    const std::string r = buildEscapeZoo(*world, g_sim, *g_hud, g_currentMapPath, escapeZoo);
    SDL_Log("[EscapeZoo] %s", r.c_str());
    if(timings)std::fclose(timings);return r.rfind("ok", 0) == 0 ? 0 : 1;
  }
  if (const char *cliffZoo = std::getenv("ZT_CLIFF_ZOO")) {
    extern std::string buildCliffZoo(World &, ZooSim &, UiGameScreen &, const std::string &, const std::string &);
    const std::string r = buildCliffZoo(*world, g_sim, *g_hud, g_currentMapPath, cliffZoo);
    SDL_Log("[CliffZoo] %s", r.c_str());
    if(timings)std::fclose(timings);return r.rfind("ok", 0) == 0 ? 0 : 1;
  }
  if (std::getenv("ZT_EVERYTHING_ZOO")) {
    int result=runEverythingZoo(*world,*g_hud,g_sim,rm,outDir,g_currentMapPath);
    if(timings)std::fclose(timings);return result;
  }
  if (std::getenv("ZT_SHOW_CHECKS")) {
    int result=runShowChecks(*world,*g_hud,g_sim,outDir,g_currentMapPath,capture);
    if(timings)std::fclose(timings);return result;
  }
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
      SDL_GetCurrentRenderOutputSize(renderer, &w, &h);
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
  // ZT_SCENARIO_PAGE=<part of a scenario's path, "scn03"; "scn03:12" also
  // plays 12 checks of its goals first>: that scenario started as from the
  // menu, its page (zooinfo4.lyt) opened and shown - the name, the story and
  // each goal's line with its state's icon
  if (const char *which = std::getenv("ZT_SCENARIO_PAGE")) {
    std::string part = which;
    int rounds = 0;
    if (size_t colon = part.find(':'); colon != std::string::npos) {
      rounds = std::atoi(part.c_str() + colon + 1);
      part.resize(colon);
    }
    const ScenarioInfo *pick = nullptr;
    for (int i = 0; const ScenarioInfo *s = sm->getScenario(i); i++)
      if (!pick && s->scenarioPath.find(part) != std::string::npos)
        pick = s;
    if (!pick || !startScenario(rm, *pick)) {
      note("scenario page: scenario not started FAILED");
      return 1;
    }
    world = g_world;
    for (int k = 0; k < rounds; k++)
      g_goals.update(1.5f);
    const UiGameScreen::ScenarioPage page = g_hud->scenarioPage();
    // (the reminder bar too, with the first goal's line, as a tutorial's popup sets it)
    if (!page.lines.empty())
      g_hud->setObjective(page.lines.front().first);
    g_hud->showPanel(803, "");
    capture("scenario_page");
    note((std::string("scenario page: ") + pick->scenarioPath + " name '" + page.name + "', " +
          std::to_string(page.story.size()) + " story chars, " + std::to_string(page.lines.size()) + " lines")
             .c_str());
    for (const auto &line : page.lines)
      note(("  " + line.first + " [" + line.second + "]").c_str());
    return 0;
  }
  // ZT_VIEW_SAVE=<file>: a saved zoo loaded and shown as it was saved (the
  // view it was left at, then the other three turns), its tanks and
  // animals listed, then quit - for looking at a player's save
  if (const char *view = std::getenv("ZT_VIEW_SAVE")) {
    bool ok = gameLoad(rm, view);
    world = g_world;
    note((std::string("loaded save ") + view + (ok ? " ok" : " FAILED")).c_str());
    if (!ok)
      return 1;
    world->getAnimals().takeMessages();
    // ours: ZT_VIEW_RESAVE=<file>: the loaded zoo saved again (the original's
    // load of a zoo this engine wrote from one of its own)
    if (const char *again = std::getenv("ZT_VIEW_RESAVE"))
      note((std::string("resaved ") + again + (gameSave(again) ? " ok" : " FAILED")).c_str());
    g_hud->showPanel(0, "");
    // (its goals: each one's state as saved, the cash, and then the goals
    // checked once - what a player's zoo would do next)
    SDL_Log("[ViewSave] map %s cash %.0f goals %zu", g_currentMapPath.c_str(), g_sim.cash(), g_goals.all().size());
    for (const Goals::Goal &goal : g_goals.all())
      SDL_Log("[ViewSave] goal %s (%s) state %d", goal.name.c_str(), goal.file.c_str(), goal.state);
    g_goals.evaluateAll();
    SDL_Log("[ViewSave] after one check: cash %.0f", g_sim.cash());
    for (const ZooSim::Month &m : g_sim.history())
      SDL_Log("[ViewSave] month %d/%d donations %.0f total %.0f", m.month + 1, m.year, m.lines[ZooSim::Donations], m.total());
    // ours: ZT_VIEW_GUIDES=<seconds>: the zoo run that long (unpaused, not
    // drawn) and its tour guides followed - each trip, its group and talk -
    // for the side by side with the original (#129); <out>/guides.txt
    if (const char *guides = std::getenv("ZT_VIEW_GUIDES")) {
      extern std::string watchGuides(World &, int);
      world->edgeScroll = false;
      world->setPaused(false);
      world->setView(gameViewRect(renderer), getUiTransform(renderer).scale);
      std::ofstream(outDir + "/guides.txt") << watchGuides(*world, std::max(1, std::atoi(guides)));
      return 0;
    }
    // ZT_VIEW_TERRAIN=<type>: a 2 x 2 patch inside the zoo painted with that
    // ground, Accept pressed, and what it cost and kept
    if (const char *paint = std::getenv("ZT_VIEW_TERRAIN")) {
      const int type = std::atoi(paint);
      WorldMap &wm = world->getMap();
      Fences &fz = world->getFences();
      TerrainTool &tt = world->getTerrainTool();
      world->setTerrainTool(true, true, type, 2, 0);
      int px = -1, py = -1;
      for (int y = 2; y < wm.getHeight() - 3 && px < 0; y++)
        for (int x = 2; x < wm.getWidth() - 3 && px < 0; x++) {
          bool clear = true;
          for (int dy = 0; dy < 2; dy++)
            for (int dx = 0; dx < 2; dx++) {
              const MapTile *t = wm.getTile(x + dx, y + dy);
              clear = clear && t && fz.insideZoo(x + dx, y + dy) && fz.exhibitAt(x + dx, y + dy) < 0 &&
                      !wm.isPath(x + dx, y + dy) && t->terrainType != type;
            }
          if (clear)
            px = x, py = y;
        }
      if (px < 0) {
        SDL_Log("[ViewSave] terrain: no clear 2 x 2 inside the zoo");
      } else {
        const double cashBefore = g_sim.cash();
        tt.press(px, py, 0, 0);
        tt.release();
        const float price = tt.cost();
        SDL_Log("[ViewSave] terrain: painted %d,%d with %d, price %.0f, cash %.0f", px, py, type, price, cashBefore);
        if (g_hud->terraformCommand)
          g_hud->terraformCommand(true);
        const MapTile *t = wm.getTile(px, py);
        SDL_Log("[ViewSave] terrain: after Accept cash %.0f (paid %.0f), tile now %d, still pending %.0f", g_sim.cash(),
                cashBefore - g_sim.cash(), t ? t->terrainType : -1, tt.cost());
      }
    }
    {
      const ZooReader &zr = world->mapReader();
      std::string states;
      for (int32_t st : zr.getGoalStates())
        states += " " + std::to_string(st);
      SDL_Log("[ViewSave] original scenario block: '%s', %zu goal states:%s", zr.getScenarioName().c_str(), zr.getGoalStates().size(),
              states.c_str());
    }
    for (const Fences::Exhibit &e : world->getFences().exhibits())
      SDL_Log("[ViewSave] exhibit %d '%s' tank %d tiles %zu wallSub %d floor %d ground %d water %.2f", e.id,
              e.name.c_str(), static_cast<int>(e.tank), e.tiles.size(), e.wallSub, e.floorHeight, e.groundHeight,
              e.water);
    for (const Animals::Member &m : world->getAnimals().members())
      SDL_Log("[ViewSave] animal %s at %.1f,%.1f exhibit %d escaped %d", m.name.c_str(), m.x, m.y, m.exhibit,
              static_cast<int>(m.escaped));
    capture("save_view");
    // ours: a check. ZT_VIEW_EXHIBIT=<id>[,<zoom>]: the view centred on that
    // exhibit (or tank) in each of the four turns, each drawn with the day
    // and night lighting off, at noon and at midnight (exhibit<id>_r<turn>_
    // off/day/night) - to tell a drawing-order fault from a lighting one
    if (const char *ex = std::getenv("ZT_VIEW_EXHIBIT")) {
      const int id = std::atoi(ex);
      const char *comma = std::strchr(ex, ',');
      float sx = 0, sy = 0;
      size_t count = 0;
      for (const Fences::Exhibit &e : world->getFences().exhibits())
        if (e.id == id)
          for (const auto &tile : e.tiles)
            sx += tile.first + 0.5f, sy += tile.second + 0.5f, count++;
      if (count) {
        // (the ground's corners round it, '*' on the exhibit's own tiles)
        int x0 = INT_MAX, y0 = INT_MAX, x1 = INT_MIN, y1 = INT_MIN;
        for (const Fences::Exhibit &e : world->getFences().exhibits())
          if (e.id == id)
            for (const auto &tile : e.tiles)
              x0 = std::min(x0, tile.first), y0 = std::min(y0, tile.second), x1 = std::max(x1, tile.first), y1 = std::max(y1, tile.second);
        for (int y = y0 - 4; y <= y1 + 4; y++) {
          std::string row;
          for (int x = x0 - 4; x <= x1 + 4; x++) {
            const MapTile *t = world->getMap().getTile(x, y);
            char cell[48];
            bool mine = false;
            for (const Fences::Exhibit &e : world->getFences().exhibits())
              mine = mine || (e.id == id && e.tiles.count({x, y}));
            std::snprintf(cell, sizeof cell, " %c%d/%d/%d/%d", mine ? '*' : ' ', t ? t->cornerHeight[0] : 0, t ? t->cornerHeight[1] : 0,
                          t ? t->cornerHeight[2] : 0, t ? t->cornerHeight[3] : 0);
            row += cell;
          }
          SDL_Log("[ViewSave] y %d (x from %d):%s", y, x0 - 4, row.c_str());
        }
        for (const PlacedObjects::Object &o : world->getObjects().objects())
          if (std::fabs(o.x - sx / count) + std::fabs(o.y - sy / count) < 16)
            SDL_Log("[ViewSave] near: %s/%s at %.2f,%.2f z %.1f facing %d", o.className.c_str(), o.typeName.c_str(), o.x, o.y, o.z, o.facing);
        // (every tile round it standing above the exhibit's ground, and where
        // its top corners land in the window: which tile is which in a picture)
        for (int y = y0 - 8; y <= y1 + 8; y++)
          for (int x = x0 - 8; x <= x1 + 8; x++)
            if (const MapTile *t = world->getMap().getTile(x, y)) {
              const int hi = std::max({t->cornerHeight[0], t->cornerHeight[1], t->cornerHeight[2], t->cornerHeight[3]});
              int ground = INT_MAX;
              for (const Fences::Exhibit &e : world->getFences().exhibits())
                if (e.id == id)
                  ground = e.tank ? e.groundHeight : INT_MAX;
              if (hi > ground)
                SDL_Log("[ViewSave] high tile %d,%d corners %d/%d/%d/%d substrate %d", x, y, t->cornerHeight[0], t->cornerHeight[1],
                        t->cornerHeight[2], t->cornerHeight[3], t->substrate);
            }
        for (const Staff::Member &m : world->getStaff().members()) {
          const Staff::Type &t = world->getStaff().types()[m.type];
          if (t.kind == Staff::Kind::Helicopter)
            SDL_Log("[ViewSave] near: DRT base at %.2f,%.2f footprint %d x %d tiles", m.x, m.y, t.footX, t.footY);
        }
        if (comma) {
          g_ZoomLevel = static_cast<float>(std::atof(comma + 1));
          world->getRenderer().getCamera().zoom = g_ZoomLevel;
        }
        g_hud->showPanel(0, "");
        const bool lightingWas = Features::dayNightLighting;
        for (int r = 0; r < 4; r++) {
          world->centreOn(sx / count, sy / count);
          // (ZT_VIEW_EXHIBIT_POINTS=x,y,h;x,y,h...: where those map points land in the window)
          if (const char *pts = std::getenv("ZT_VIEW_EXHIBIT_POINTS")) {
            world->setView(gameViewRect(renderer), getUiTransform(renderer).scale);
            world->draw(renderer);
            for (std::string rest = pts; !rest.empty();) {
              const std::string one = rest.substr(0, rest.find(';'));
              rest = rest.find(';') == std::string::npos ? "" : rest.substr(rest.find(';') + 1);
              float px = 0, py = 0, ph = 0;
              if (std::sscanf(one.c_str(), "%f,%f,%f", &px, &py, &ph) == 3) {
                int qx = 0, qy = 0;
                world->pointToWindow(px, py, ph, qx, qy);
                float rx = 0, ry = 0, rd = 0, wx = 0, wy = 0;
                world->getRenderer().worldToScreenF(px, py, ph, rx, ry, rd);
                // (and the map point the renderer finds under that spot of the window)
                const bool hit = world->getRenderer().screenToWorld(rx, ry, world->getMap(), wx, wy);
                SDL_Log("[ViewSave] turn %d: point %.1f,%.1f height %.1f at %d,%d (render %.0f,%.0f): ground there %s %.2f,%.2f", r,
                        px, py, ph, qx, qy, rx, ry, hit ? "at" : "none", wx, wy);
              }
            }
          }
          for (int mode = 0; mode < 3; mode++) {
            Features::dayNightLighting = mode > 0;
            world->getLighting().previewHour = mode == 2 ? 0 : 12;
            for (int i = 0; i < 30; i++) { // (the sun's maps made ahead settle)
              world->setView(gameViewRect(renderer), getUiTransform(renderer).scale);
              world->draw(renderer);
              SDL_RenderPresent(renderer);
            }
            static const char *const kModes[] = {"off", "day", "night"};
            capture(("exhibit" + std::to_string(id) + "_r" + std::to_string(r) + "_" + kModes[mode]).c_str());
          }
          world->rotateView(1);
        }
        Features::dayNightLighting = lightingWas;
        world->getLighting().previewHour = -1;
      }
      SDL_Log("[ViewSave] exhibit %d centred at %.1f,%.1f (%zu tiles)", id, count ? sx / count : 0.0f, count ? sy / count : 0.0f, count);
    }
    for (int pass = 0; pass < (std::getenv("ZT_VIEW_TWICE") ? 2 : 1); pass++) {
      if (pass == 1) {
        gameLoad(rm, view);
        world = g_world;
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
    world->rotateView(1);
    // (and zoomed in and out, as the wheel does: a tenth at a time)
    if (std::getenv("ZT_VIEW_ZOOMS")) {
      float saved = world->getRenderer().getCamera().zoom;
      for (float z : {0.5f, 0.7f, 0.9f, 1.1f, 1.3f, 1.6f, 2.0f, 2.5f}) {
        g_ZoomLevel = z;
        world->getRenderer().getCamera().zoom = z;
        char nm[32];
        std::snprintf(nm, sizeof nm, "save_zoom_%d", static_cast<int>(std::lround(z * 100)));
        capture(nm);
      }
      g_ZoomLevel = saved;
      world->getRenderer().getCamera().zoom = saved;
    }
    // ours: a check. ZT_VIEW_TOUR=<zoom>: each kind of animal in turn, the
    // view centred on one at that zoom with the zoo running for two seconds
    // (it walks, swims and animates), then a picture (tour_<n>_<key>); then
    // the frame times and the HD art in use - for art mods, close up
    if (const char *tour = std::getenv("ZT_VIEW_TOUR")) {
      const float zoom = std::max(0.5f, static_cast<float>(std::atof(tour)));
      world->edgeScroll = false;
      world->setPaused(false);
      world->setView(gameViewRect(renderer), getUiTransform(renderer).scale);
      Animation::setGradualZoomedIn(true); // (as in play)
      const bool *keys = SDL_GetKeyboardState(nullptr);
      const std::vector<Animals::Member> members = world->getAnimals().members(); // (they move)
      std::set<int> seen;
      int n = 0, frames = 0;
      double total = 0, worst = 0;
      // (ZT_VIEW_TOUR_KINDS=<n>: only the first n kinds - a shorter run to
      // compare settings by)
      const size_t kindsMax = std::getenv("ZT_VIEW_TOUR_KINDS") ? std::max(1, std::atoi(std::getenv("ZT_VIEW_TOUR_KINDS"))) : SIZE_MAX;
      for (const Animals::Member &m : members) {
        if (!seen.insert(m.type).second)
          continue;
        if (seen.size() > kindsMax)
          break;
        g_ZoomLevel = zoom;
        world->getRenderer().getCamera().zoom = zoom;
        for (int i = 0; i < 60; i++) {
          const Uint64 f0 = SDL_GetPerformanceCounter();
          world->update(keys, 1.0f / 30.0f);
          world->centreOnAnimal(m.id);
          SDL_RenderClear(renderer);
          world->draw(renderer);
          SDL_RenderPresent(renderer);
          const double ms = (SDL_GetPerformanceCounter() - f0) * 1000.0 / SDL_GetPerformanceFrequency();
          total += ms;
          worst = std::max(worst, ms);
          frames++;
        }
        const auto &types = world->getAnimals().types();
        const std::string key = m.type >= 0 && m.type < static_cast<int>(types.size()) ? types[m.type].key : "unknown";
        char nm[96];
        std::snprintf(nm, sizeof nm, "tour_%02d_%s.png", n++, key.c_str());
        // (the map alone, as the last frame drew it: no interface or messages over it)
        SDL_RenderClear(renderer);
        world->draw(renderer);
        int pw = 0, ph = 0;
        SDL_GetCurrentRenderOutputSize(renderer, &pw, &ph);
        if (SDL_Surface *shot = SDL_CreateSurface(pw, ph, SDL_PIXELFORMAT_RGBA32)) {
          if (ZT_RenderReadPixels(renderer, nullptr, SDL_PIXELFORMAT_RGBA32, shot->pixels, shot->pitch) == 0)
            IMG_SavePNG(shot, (outDir + "/" + nm).c_str());
          SDL_DestroySurface(shot);
        }
        SDL_RenderPresent(renderer);
      }
      SDL_Log("[ViewSave] tour: %d kinds of animal at zoom %.1f, %d frames, %.1f ms a frame, worst %.1f", n, zoom,
              frames, frames ? total / frames : 0.0, worst);
      SDL_Log("[ViewSave] HD art: %s", HdArt::stats().c_str());
      SDL_Log("[ViewSave] zoomed-in frames kept: %zu MB", Animation::zoomedInBytes() >> 20);
      SDL_Log("[ViewSave] memory: %s", Animation::memoryStats().c_str());
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

  // The zoom buttons' three levels (zoo.exe 0x4b081b, 0x4b0779): from
  // normal, Zoom In goes to double and greys; Zoom Out back down to half,
  // and it greys there
  {
    Camera &cam = world->getRenderer().getCamera();
    const float saved = cam.zoom;
    cam.zoom = 1.0f;
    updateZoomButtons(world);
    UiElement *in = g_hud->getElementById(1007), *out = g_hud->getElementById(1023);
    const bool normal = in && out && !in->isDisabled() && !out->isDisabled();
    world->zoomStep(+1);
    updateZoomButtons(world);
    const bool twice = cam.zoom == 2.0f && in->isDisabled() && !out->isDisabled();
    world->zoomStep(-1);
    world->zoomStep(-1);
    updateZoomButtons(world);
    const bool half = cam.zoom == 0.5f && !in->isDisabled() && out->isDisabled();
    check("zoom buttons: normal, double (Zoom In greys), half (Zoom Out greys)", normal && twice && half);
    cam.zoom = saved;
    g_ZoomLevel = saved;
    updateZoomButtons(world);
  }
  // Opening Research puts the bulldozer away (zoo.exe research_Panel_show
  // 0x4a031a: bulldozeToolOff 0x443b60)
  if (UiButton *dozer = dynamic_cast<UiButton *>(g_hud->getElementById(1028))) {
    g_hud->showPanel(0, "");
    dozer->setToggledOn(true);
    g_hud->showPanel(15, "");
    check("opening Research puts the bulldozer away", !g_hud->bulldozerOn());
    g_hud->showPanel(0, "");
  }

  // The Message List holds the message bar (zoo.exe message_Panel_show
  // 0x4a03b4 sets its +0x450, message_Panel_hide 0x4a03fb clears it)
  {
    g_hud->setPanelOpen(29, false);
    const bool before = g_hud->messageBarHeld();
    g_hud->setPanelOpen(29, true);
    const bool open = g_hud->messageBarHeld();
    g_hud->setPanelOpen(29, false);
    check("the Message List open holds the message bar, closed lets it go",
          !before && open && !g_hud->messageBarHeld());
  }

  // The starting cash's arrows grey at the ends (zoo.exe mapselec_UpSpinner_click
  // 0x616350, mapselec_DownSpinner_click 0x6163b2)
  {
    const int savedCash = Settings::getInt("UI", "MSStartingCash", 50000);
    UiLayout *menu = new UiLayout(rm->getIniReader(getLayoutPath(rm, "mapselec.lyt")), rm);
    UiElement *up = menu->getElementById(11511), *down = menu->getElementById(11512);
    loadStartingCash();
    setStartingCash(CASH_MAX - CASH_STEP);
    stepStartingCash(menu, 1);
    const bool atMax = up && down && g_currentStartingCash == CASH_MAX && up->isDisabled() && !down->isDisabled();
    stepStartingCash(menu, -1);
    const bool backDown = up && !up->isDisabled() && g_currentStartingCash == CASH_MAX - CASH_STEP;
    setStartingCash(CASH_MIN + CASH_STEP);
    stepStartingCash(menu, -1);
    const bool atMin = up && down && g_currentStartingCash == CASH_MIN && down->isDisabled() && !up->isDisabled();
    check("starting cash: Up greys at the most, Down at the least, the other arrow comes back",
          up && down && atMax && backDown && atMin);
    Settings::setInt("UI", "MSStartingCash", savedCash);
    delete menu;
  }

  // The Help window (zoo.exe help_Panel_show 0x484a6b, help_TopicList_click
  // 0x48482b, openHelp 0x484a32): help.cfg's topics, About first with its
  // Version put in, the one a topic picked shows, closed and opened again
  // the one it was left on
  {
    g_hud->setPanelOpen(38, false);
    const size_t topics = g_hud->helpTopicCount();
    g_hud->openHelp(0);
    const bool about = g_hud->isPanelOpen(38) && g_hud->helpTopicShown() == 0 &&
                       g_hud->helpTopicText().find("Zoo Tycoon") != std::string::npos &&
                       g_hud->helpTopicText().find("%s") == std::string::npos;
    g_hud->openHelp(2);
    const std::string quick = g_hud->helpTopicText();
    g_hud->setPanelOpen(38, false);
    g_hud->openHelp(-1);
    const bool left = g_hud->helpTopicShown() == 2 && g_hud->helpTopicText() == quick;
    check("the Help window: about, a topic picked, the topic it was left on",
          topics >= 50 && about && !quick.empty() && quick.find("Zoo Tycoon") != 0 && left);
    g_hud->setPanelOpen(38, false);
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
  // The thing held stays shown, and turned, at the cursor when it's turned
  // (Q / E) or the view is turned (#103: it vanished for a moment): ours
  {
    int cost = 0, facing = 0;
    const std::string held = g_hud->objectToolFile(cost, facing);
    float cx = 0, cy = 0;
    int sx = 0, sy = 0;
    const bool placed = !held.empty() && world->viewCentre(cx, cy);
    bool shownEveryTime = false, turnedEveryTime = false, viewKept = false;
    if (placed) {
      world->setObjectTool(held, cost, facing);
      world->pointToWindow(cx + 2.0f, cy + 1.0f, 0, sx, sy); // (off the middle, which a turn keeps)
      world->mouseMove(sx, sy);
      shownEveryTime = world->objectGhostShown();
      turnedEveryTime = true;
      for (int step : {1, 1, 1, 1, -1, -1, -1, -1}) { // (round again: the panel keeps its facing)
        const int before = world->objectGhostShown() ? world->ghostFacingOf() : -1;
        g_hud->rotateHeld(step);
        // (the frame's own: the picked thing and its facing handed on)
        const std::string again = g_hud->objectToolFile(cost, facing);
        world->setObjectTool(again, cost, facing);
        shownEveryTime = shownEveryTime && world->objectGhostShown();
        turnedEveryTime = turnedEveryTime && world->ghostFacingOf() != before;
      }
      // (the view turned under it: shown again where the cursor is, with no
      // move of the mouse)
      world->rotateView(1);
      float gx = 0, gy = 0;
      viewKept = world->objectGhostShown() && world->groundAt(sx, sy, gx, gy) &&
                 world->ghostCentreIs(gx, gy);
      world->rotateView(-1);
      world->setObjectTool("", 0, 0);
    }
    check("a thing held shows at the cursor, turned, in the frame it is turned (Q / E; #103)",
          placed && shownEveryTime && turnedEveryTime);
    check("a thing held shows at the cursor again after the view turns, the mouse not moved (#103)",
          placed && viewKept);
  }
  // (each panel keeps its own facing, as the original - zoo.exe 0x48e40f,
  // 0x4e5134: Adopt turned once on, Objects still as it was)
  {
    int cost = 0, adoptFacing = -1, objectsFacing = -1;
    const bool adoptHeld = !g_hud->objectToolFile(cost, adoptFacing).empty();
    g_hud->showPanel(4, "structures");
    const bool objectsHeld = !g_hud->objectToolFile(cost, objectsFacing).empty();
    check(("each buy panel turns its own items (Adopt " + std::to_string(adoptFacing) + ", Objects " +
           std::to_string(objectsFacing) + ")").c_str(),
          adoptHeld && objectsHeld && adoptFacing == 1 && objectsFacing == 0);
  }

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
  {
    // (ours: compressed HD textures - GpuCompressed.hpp)
    const std::string why = GpuCompressed::selfCheck(renderer);
    if (!why.empty())
      SDL_Log("[PanelShots] compressed textures: %s", why.c_str());
    check("compressed HD textures: encoded close, drawn as decoded", why.empty());
  }
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
  // (as typed, no more than cMaxAdultAdmissionPrice: $100 - zoo.exe
  // setAdmissionPrice 0x4bbf10)
  key(InputEvent::TEXT_INPUT, 0, "130");
  key(InputEvent::KEY_DOWN, SDLK_RETURN, "");
  g_hud->setZooInfo(g_hud->zooInfo());
  check("typed admission $130 kept to $100.00 / $50.00",
        textOf(4193) == "$100.00" && textOf(4194) == "$50.00");

  // Game Options, Extras (an addition): a switch for each extra; clicked,
  // it flips and is kept in zoo.ini's [Extras] - clicked again, as it was
  {
    g_hud->showPanel(5, "");
    g_hud->showTab(5, 1509);
    capture("gameopts_extras_test");
    // (the page lists the Options window's switch first, then the rest in
    // Features' order)
    int at = 1;
    for (const Features::Switch &f : Features::all()) {
      const std::string name = f.name;
      if (name == "hotkeys")
        break;
      // (as UiGameScreen's extrasOrder: the cheat and the lighting and clock
      // switches live in the Options window only)
      if (name != "optionswindow" && name != "infinitemoney" && name != "daynight" && name != "gametime")
        at++;
    }
    bool was = Features::hotkeys;
    click(9050 + at);
    bool flipped = Features::hotkeys != was && Settings::get("Extras", "hotkeys") == (was ? "0" : "1");
    click(9050 + at);
    check("Extras: a switch flips its extra and is kept in zoo.ini",
          flipped && Features::hotkeys == was && Settings::get("Extras", "hotkeys") == (was ? "1" : "0"));
    // (#104: more switches than the panel holds go on a second page, turned
    // by the arrows under them; each inside the panel, its label's id its
    // own - the last switch, on the last page, flips its own extra)
    std::vector<const Features::Switch *> listed;
    for (const Features::Switch &f : Features::all()) {
      const std::string name = f.name;
      if (name == "optionswindow")
        listed.insert(listed.begin(), &f);
      else if (name != "infinitemoney" && name != "daynight" && name != "gametime")
        listed.push_back(&f);
    }
    int last = static_cast<int>(listed.size()) - 1;
    UiElement *first = g_hud->getElementById(9050), *lastTick = g_hud->getElementById(9050 + last);
    UiElement *sameRow = g_hud->getElementById(9050 + last % 12); // (on the first page)
    UiElement *note = g_hud->getElementById(9049);
    bool paged = listed.size() <= 12 || (g_hud->getElementById(8891) && first && lastTick && lastTick->isHidden());
    if (listed.size() > 12 && paged) {
      int rowY = sameRow->getLastRect().y, noteY = note ? note->getLastRect().y : 0;
      click(8891);
      capture("gameopts_extras_page2");
      bool turned = first->isHidden() && !lastTick->isHidden();
      // (in the first page's rows, above the note: inside the panel)
      bool inside = lastTick->getLastRect().y == rowY && lastTick->getLastRect().y < noteY;
      bool lastWas = *listed[last]->value;
      click(9050 + last);
      bool lastFlipped = *listed[last]->value != lastWas;
      click(9050 + last);
      click(8890);
      paged = paged && turned && inside && lastFlipped && *listed[last]->value == lastWas && !first->isHidden();
    }
    bool ownIds = true;
    for (int k = 0; k <= last; k++)
      ownIds = ownIds && dynamic_cast<UiText *>(g_hud->getElementById(8900 + k)) &&
               dynamic_cast<UiButton *>(g_hud->getElementById(9050 + k));
    check("Extras: every switch on a page inside the panel, the arrows turn it, labels on their own ids",
          paged && ownIds);
    g_hud->showTab(5, 1506); // (back on File Options, for the game menu's tests)
    g_hud->showPanel(0, "");
  }

  // The Options window (an addition): the disk button opens it in place of
  // the side panel, on its Game page; Esc closes it; Interface Size picks a
  // size the window can show, and Auto again
  {
    Features::optionsWindow = true;
    g_hud->showPanel(5, "");
    bool opened = g_hud->optionsOpen() && !g_hud->isPanelOpen(5);
    capture("options_window_game");
    check("the Options window opens in place of the Game Options panel", opened);
    // (its Gameplay page: every switch above the description box)
    if (g_hud->options) {
      // (its Display page: the graphics API row offers SDL3's Vulkan too)
      g_hud->options->showPage(1);
      capture("options_window_display");
      check("the Display page offers Auto, Direct3D 11, OpenGL and Vulkan", Display::rendererChoice() >= 0 && Display::rendererChoice() <= 3);
      g_hud->options->showPage(7);
      capture("options_window_gameplay");
      g_hud->options->showPage(UiOptions::kModsPage);
      capture("options_window_mods");
      check("Mods keeps its existing page ID", g_hud->options->shownPage() == UiOptions::kModsPage);
      g_hud->options->showPage(UiOptions::kUpdatesPage);
      capture("options_window_updates");
      check("Updates opens separately above Done", g_hud->options->shownPage() == UiOptions::kUpdatesPage);
      g_hud->options->showPage(0);
    }
    float before = RenderSettings::uiScaleSetting;
    UiOptions *o = g_hud->options;
    std::vector<Input> esc(1);
    esc[0].type = InputType::BUTTON;
    esc[0].event = InputEvent::KEY_ESCAPE;
    // (as the game's loop: Esc with nothing open opens it, and the same
    // frame's inputs then reach it - that Esc mustn't close it again)
    o->handleInputs(esc);
    check("the Esc that opened the Options window leaves it open", g_hud->optionsOpen());
    o->handleInputs(esc);
    check("Esc closes the Options window", !g_hud->optionsOpen());
    RenderSettings::uiScaleSetting = before;
    Features::optionsWindow = false;
  }

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
            if (ok) {
              // (a path through it lifted first, as a player would: a
              // fence can't cross one - the nofenceonpath switch - and the
              // loop wouldn't close)
              for (int y = y0; y < y0 + n; y++)
                for (int x = x0; x < x0 + n; x++)
                  if (map.isPath(x, y))
                    map.setPath(x, y, "");
              return std::make_pair(x0, y0);
            }
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
    // The bulldozer over a path tile beside the tank, on each side: its red
    // shows (the tank redraws the ground round it, which once covered it)
    {
      const std::pair<int, int> beside[4] = {{tx, ty - 1}, {tx + 2, ty}, {tx, ty + 2}, {tx - 1, ty}};
      WorldMap &wm = world->getMap();
      for (auto [px, py] : beside)
        wm.setPath(px, py, "path");
      world->centreOn(tx + 1.0f, ty + 1.0f);
      // (how much of a tile's diamond turned red: red over green up by 40)
      auto capturePixels = [&](const char *name) {
        capture(name);
        std::vector<int> redness;
        if (SDL_Surface *s = SDL_LoadBMP((outDir + "/" + name + ".bmp").c_str())) {
          if (SDL_Surface *c = SDL_ConvertSurface(s, SDL_PIXELFORMAT_ARGB8888)) {
            redness.resize(static_cast<size_t>(c->w) * c->h);
            for (int y = 0; y < c->h; y++)
              for (int x = 0; x < c->w; x++) {
                const Uint32 p = static_cast<Uint32 *>(c->pixels)[y * (c->pitch / 4) + x];
                redness[static_cast<size_t>(y) * c->w + x] =
                    static_cast<int>((p >> 16) & 0xff) - static_cast<int>((p >> 8) & 0xff);
              }
            SDL_DestroySurface(c);
          }
          SDL_DestroySurface(s);
        }
        return redness;
      };
      int outW = 0, outH = 0;
      SDL_GetCurrentRenderOutputSize(renderer, &outW, &outH);
      // (each tile at its middle and toward its far side, away from the
      // tank: the wall's own face covers the near half of one behind the
      // tank, as its art does, and there it's the wall that's picked - one
      // of the two must pick the path, and its red must show)
      const std::pair<float, float> away[4] = {{0, -1}, {1, 0}, {0, 1}, {-1, 0}};
      for (int side = 0; side < 4; side++) {
        auto [px, py] = beside[side];
        // (no path laid there - a slope, the edge of a cliff: nothing to pick)
        if (!wm.isPath(px, py)) {
          note(("no path could be laid at " + std::to_string(px) + "," + std::to_string(py)).c_str());
          continue;
        }
        const MapTile *tile = wm.getTile(px, py);
        const float h = tile ? static_cast<float>(tile->height) : 0;
        // (one below a raised tank: its wall runs down the cliff over it, and
        // the wall may be what's under the cursor there)
        const Fences::Exhibit *raised = fences.exhibit(fences.exhibitAt(tx, ty));
        const bool underWall = raised && h < raised->groundHeight;
        // (the tile's diamond on screen)
        int cx[4], cy[4];
        const float ox[4] = {0, 1, 1, 0}, oy[4] = {0, 0, 1, 1};
        for (int k = 0; k < 4; k++)
          world->pointToWindow(px + ox[k], py + oy[k], h, cx[k], cy[k]);
        auto inDiamond = [&](int x, int y) {
          bool neg = false, pos = false;
          for (int k = 0; k < 4; k++) {
            const int a = k, b = (k + 1) % 4;
            const long cr = static_cast<long>(cx[b] - cx[a]) * (y - cy[a]) - static_cast<long>(cy[b] - cy[a]) * (x - cx[a]);
            neg = neg || cr < 0;
            pos = pos || cr > 0;
          }
          return !(neg && pos);
        };
        std::string seen;
        bool picked = false;
        int shown = 0;
        for (int far = 0; far < 2 && !picked; far++) {
          int sx, sy;
          world->pointToWindow(px + 0.5f + away[side].first * 0.3f * far, py + 0.5f + away[side].second * 0.3f * far,
                               h, sx, sy);
          world->setBulldozer(false);
          world->mouseMove(sx, sy);
          const std::vector<int> plain = capturePixels("bulldoze_path_plain");
          world->setBulldozer(true);
          world->mouseMove(sx, sy);
          picked = world->bulldozePathTile() == std::make_pair(px, py);
          seen += std::string(far ? ", far side: " : " middle: ") + (picked ? "the path" : "not the path");
          if (!picked)
            continue;
          const std::string name = "bulldoze_path_by_tank_" + std::to_string(side);
          const std::vector<int> lit = capturePixels(name.c_str());
          if (lit.size() == plain.size() && lit.size() == static_cast<size_t>(outW) * outH)
            for (int y = 0; y < outH; y++)
              for (int x = 0; x < outW; x++)
                if (inDiamond(x, y) && lit[static_cast<size_t>(y) * outW + x] > plain[static_cast<size_t>(y) * outW + x] + 40)
                  shown++;
        }
        if (!picked && underWall)
          note(("the path at " + std::to_string(px) + "," + std::to_string(py) +
                " lies under the raised tank's wall, which is picked").c_str());
        else
          check(("the bulldozer picks a path tile beside the tank and its red shows (" + std::to_string(px) + "," +
                 std::to_string(py) + seen + "; " + std::to_string(shown) + " pixels red)").c_str(),
                picked && shown > 30);
      }
      world->setBulldozer(false);
      for (auto [px, py] : beside)
        wm.setPath(px, py, "");
    }
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
                  SDL_RenderPoint(renderer, x, y);
                  a->drawAnchored(renderer, x, y, dir, nullptr, 0);
                }
            for (Animation *a : {ft.ladder, ft.ladderOut, ft.platform}) {
              if (a) {
                float x = 30.0f + col * 48.0f, y = 60.0f + row * 90.0f;
                a->drawAnchored(renderer, x, y, dir, nullptr, 0);
                SDL_SetRenderDrawColor(renderer, 255, 0, 0, 255);
                SDL_RenderPoint(renderer, x, y);
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
              SDL_RenderPoint(renderer, x, y);
              int w = 0, h = 0, ox = 0, oy = 0;
              a->queryTexture(CompassDirection::N, &w, &h);
              a->anchorOffset(CompassDirection::N, &ox, &oy);
              SDL_Log("[TankLab] water %s %dx%d anchor off %d,%d frames %d", part, w, h, ox, oy, a->frameCount());
            }
            col++;
          }
        }
        SDL_Surface *sf = SDL_CreateSurface(2800, 760, SDL_PIXELFORMAT_RGB24);
        ZT_RenderReadPixels(renderer, nullptr, SDL_PIXELFORMAT_RGB24, sf->pixels, sf->pitch);
        SDL_SaveBMP(sf, (outDir + "/tankpieces.bmp").c_str());
        SDL_DestroySurface(sf);
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
        SDL_Surface *sf = SDL_CreateSurface(760, 200, SDL_PIXELFORMAT_RGB24);
        ZT_RenderReadPixels(renderer, nullptr, SDL_PIXELFORMAT_RGB24, sf->pixels, sf->pitch);
        SDL_SaveBMP(sf, (outDir + "/waterparts.bmp").c_str());
        SDL_DestroySurface(sf);
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
              if (MapTile *t = map.getTileMutable(x, y)) {
                for (int c = 0; c < 4; c++)
                  t->cornerHeight[c] = h;
                // (its own height too: a saved zoo gives the original this,
                // and a tile left a unit off stood the tanks a unit taller
                // in zoo.exe than here)
                t->height = h;
              }
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
      // ours: a check. ZT_TANK_LAB_SLOPE: the patch a ridge instead, up a
      // unit a tile eastwards to the shared wall and down again (#274: each
      // tank on a slope, its floor from its tiles' average or its highest
      // corner), with a path tile at each corner outside the tanks to find
      // them by in the original
      if (std::getenv("ZT_TANK_LAB_SLOPE") && lx >= 0) {
        WorldMap &map = world->getMap();
        const int h = map.getTile(lx, ly)->cornerHeight[0];
        auto ridge = [&](int vx) { return h + std::max(0, std::min(vx - lx, lx + 8 - vx)); };
        for (int y = ly - 1; y <= ly + 5; y++)
          for (int x = lx - 1; x <= lx + 9; x++)
            if (MapTile *t = map.getTileMutable(x, y)) {
              t->height = ridge(x);
              t->cornerHeight[CORNER_X0Y0] = t->cornerHeight[CORNER_X0Y1] = ridge(x);
              t->cornerHeight[CORNER_X1Y0] = t->cornerHeight[CORNER_X1Y1] = ridge(x + 1);
            }
        for (auto [px, py] : {std::pair{lx - 1, ly - 1}, {lx + 8, ly - 1}, {lx - 1, ly + 4}, {lx + 8, ly + 4}})
          map.setPath(px, py, "path");
        map.touch();
        world->reindex();
      }
      // ZT_TANK_LAB_BARE=<file.zoo>: the ground saved before the tanks are
      // laid, shared, for the original to build its own tanks on
      if (const char *bare = std::getenv("ZT_TANK_LAB_BARE"); bare && g_hud && lx >= 0) {
        world->centreOn(lx + 4.0f, ly + 2.0f);
        SDL_Log("[TankLab] bare ground saved %s: %d", bare,
                static_cast<int>(SaveGame::save(bare, *world, g_sim, *g_hud, g_currentMapPath)));
      }
      SDL_Log("[TankLab] spot %d,%d: %s, %s", lx, ly, ka.c_str(), kb.c_str());
      // ours: a check. ZT_TANK_LAB_COLUMN=<units>: a tile diagonally in front
      // of the tanks' nearest corner raised that many units (a rock column
      // by a tank sunk into a pit, as on the Excavation map), the map drawn
      // before the tanks are laid and after: every pixel of the column the
      // same - the tank's corner post, rail and glass behind it hidden by it
      // (measured: the original draws such a column over them)
      std::vector<std::array<int, 3>> columnBefore; // (x, y, colour)
      std::vector<std::array<int, 3>> columnRaised; // (x, y, units up)
      int columnX = -1, columnY = -1;
      float columnH = 0;
      auto drawMapOnly = [&](std::vector<uint32_t> &px, int &pw, int &ph) {
        world->setView(gameViewRect(renderer), getUiTransform(renderer).scale);
        world->getRenderer().centreOnWorld(lx + 4.0f, ly + 2.0f, columnH);
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
        SDL_RenderClear(renderer);
        world->draw(renderer);
        SDL_GetCurrentRenderOutputSize(renderer, &pw, &ph);
        px.assign(size_t(pw) * ph, 0);
        ZT_RenderReadPixels(renderer, nullptr, SDL_PIXELFORMAT_ARGB8888, px.data(), pw * 4);
        SDL_RenderPresent(renderer);
      };
      // (a raised tile's outline in the window, two pixels in from its edges,
      // from its foot to its top)
      auto columnHull = [&](int tx, int ty, float top) {
        std::vector<SDL_FPoint> pts;
        for (int k = 0; k < 8; k++) {
          int qx = 0, qy = 0;
          world->pointToWindow(tx + float((k & 3) == 1 || (k & 3) == 2), ty + float((k & 3) >= 2),
                               k < 4 ? columnH + top : columnH, qx, qy);
          pts.push_back({float(qx), float(qy)});
        }
        // (convex: inside every edge of the hull, by its winding)
        std::sort(pts.begin(), pts.end(), [](SDL_FPoint a, SDL_FPoint b) { return a.x < b.x || (a.x == b.x && a.y < b.y); });
        std::vector<SDL_FPoint> hull;
        auto cross = [](SDL_FPoint o, SDL_FPoint a, SDL_FPoint b) { return (a.x - o.x) * (b.y - o.y) - (a.y - o.y) * (b.x - o.x); };
        for (int pass = 0; pass < 2; pass++) {
          const size_t start = hull.size();
          for (size_t i = 0; i < pts.size(); i++) {
            const SDL_FPoint p = pass ? pts[pts.size() - 1 - i] : pts[i];
            while (hull.size() >= start + 2 && cross(hull[hull.size() - 2], hull.back(), p) <= 0)
              hull.pop_back();
            hull.push_back(p);
          }
          hull.pop_back();
        }
        return hull;
      };
      auto inColumn = [](const std::vector<SDL_FPoint> &hull, float px, float py) {
        for (size_t i = 0; i < hull.size(); i++) {
          const SDL_FPoint a = hull[i], b = hull[(i + 1) % hull.size()];
          const float len = std::hypot(b.x - a.x, b.y - a.y);
          if (len > 0.01f && ((b.x - a.x) * (py - a.y) - (b.y - a.y) * (px - a.x)) / len < 2.0f)
            return false;
        }
        return hull.size() >= 3;
      };
      // ZT_TANK_LAB_COLUMN=<units>,<tiles out>,<step units>: the column that
      // many tiles out along the corner's diagonal (2 unless set: a tile of
      // ground between it and the corner - on the corner itself the walls
      // were laid round it), and a lower step of that height a tile further
      // out in front of it, its pixels the same too (the ground drawn again
      // over a tank painted the rock over the ground standing in front of it:
      // a path by an Excavation pit cut off by a block of rock)
      if (const char *col = std::getenv("ZT_TANK_LAB_COLUMN"); col && lx >= 0) {
        int units = 0, out = 2, stepUnits = 0;
        std::sscanf(col, "%d,%d,%d", &units, &out, &stepUnits);
        out = std::max(2, out);
        WorldMap &map = world->getMap();
        columnH = static_cast<float>(map.getTile(lx, ly)->cornerHeight[0]);
        float best = -1e30f;
        int dirX = 0, dirY = 0;
        // (each corner vertex, the tile just outside it, the way out)
        for (auto [cx2, cy2, ox, oy, dx, dy] : {std::array<int, 6>{lx, ly, -1, -1, -1, -1}, {lx + 8, ly, 0, -1, 1, -1},
                                                {lx + 8, ly + 4, 0, 0, 1, 1}, {lx, ly + 4, -1, 0, -1, 1}}) {
          float sx, sy, d;
          world->getRenderer().worldToScreenF(float(cx2), float(cy2), columnH, sx, sy, d);
          if (d > best)
            best = d, columnX = cx2 + ox + dx * (out - 1), columnY = cy2 + oy + dy * (out - 1), dirX = dx, dirY = dy;
        }
        std::vector<std::array<int, 3>> &raised = columnRaised;
        raised.push_back({columnX, columnY, units});
        if (stepUnits > 0)
          raised.push_back({columnX + dirX, columnY + dirY, stepUnits});
        auto raise = [&](bool up) {
          for (const auto &[tx, ty, by] : raised)
            if (MapTile *t = map.getTileMutable(tx, ty))
              for (int c = 0; c < 4; c++)
                t->cornerHeight[c] = static_cast<int>(columnH) + (up ? by : 0);
          map.touch();
        };
        raise(true);
        std::vector<uint32_t> px;
        int pw = 0, ph = 0;
        drawMapOnly(px, pw, ph);
        std::vector<std::vector<SDL_FPoint>> hulls;
        for (const auto &[tx, ty, up] : raised)
          hulls.push_back(columnHull(tx, ty, static_cast<float>(up)));
        for (int y = 0; y < ph; y++)
          for (int x = 0; x < pw; x++)
            for (const auto &hull : hulls)
              if (inColumn(hull, float(x), float(y))) {
                columnBefore.push_back({x, y, int(px[size_t(y) * pw + x])});
                break;
              }
        SDL_Log("[TankLab] column %d,%d raised %d (step %d): %zu pixels", columnX, columnY, units, stepUnits, columnBefore.size());
        // (level again while the tanks are laid: they're laid by the mouse,
        // and a column in the way took the clicks - raised again after)
        raise(false);
      }
      if (lx >= 0) {
        lay(ka.c_str(), {{lx, ly}, {lx + 4, ly}, {lx + 4, ly + 4}, {lx, ly + 4}, {lx, ly}});
        enter();
        // (ZT_TANK_LAB_ONE: the first tank alone)
        if (std::getenv("ZT_TANK_LAB_ONE"))
          ;
        else if (std::getenv("ZT_TANK_LAB_GAP"))
          lay(kb.c_str(), {{lx + 5, ly}, {lx + 9, ly}, {lx + 9, ly + 4}, {lx + 5, ly + 4}, {lx + 5, ly}});
        else
          lay(kb.c_str(), {{lx + 4, ly}, {lx + 8, ly}, {lx + 8, ly + 4}, {lx + 4, ly + 4}});
        enter();
        for (int i = 0; i < 40; i++)
          world->getFences().update(1.0f);
        for (const Fences::Exhibit &x : fences.exhibits())
          if (x.tank) {
            std::string hs;
            for (int y = ly; y < ly + 4; y++)
              for (int tx = lx; tx < lx + 8; tx++)
                if (x.tiles.count({tx, y}) && y == ly + 1)
                  hs += " " + std::to_string(world->getMap().getTile(tx, y)->height);
            SDL_Log("[TankLab] made: tank %d floor %d ground %d top %.2f; row %d tile heights%s", x.id, x.floorHeight,
                    x.groundHeight, x.wallTop(), ly + 1, hs.c_str());
          }
        // ZT_TANK_LAB_RAISE=<units>: the walls up so the tank stands ABOVE its
        // ground (what a player builds), instead of sunk down into a hole where
        // the walls are drawn over the grass and nothing below the rim shows.
        // Default 8: three whole units of wall above the ground.
        if (std::getenv("ZT_TANK_LAB_RAISE")) {
          int raise = std::atoi(std::getenv("ZT_TANK_LAB_RAISE"));
          if (raise > 0)
            for (const Fences::Exhibit &x : fences.exhibits())
              if (x.tank && x.tiles.count({lx, ly}))
                world->getFences().adjustWall(x.id, raise);
          for (int i = 0; i < 40; i++)
            world->getFences().update(1.0f);
          for (const Fences::Exhibit &x : fences.exhibits())
            if (x.tank)
              SDL_Log("[TankLab] raised: tank %d wallSub %d floor %d ground %d top %.2f level %.2f", x.id, x.wallSub,
                      x.floorHeight, x.groundHeight, x.wallTop(), fences.waterLevel(x));
        }
        if (!columnBefore.empty()) {
          for (const auto &[tx, ty, by] : columnRaised)
            if (MapTile *t = world->getMap().getTileMutable(tx, ty))
              for (int c = 0; c < 4; c++)
                t->cornerHeight[c] = static_cast<int>(columnH) + by;
          world->getMap().touch();
          std::vector<uint32_t> px;
          int pw = 0, ph = 0;
          drawMapOnly(px, pw, ph);
          size_t changed = 0;
          for (const auto &[x, y, c] : columnBefore)
            if (px[size_t(y) * pw + x] != uint32_t(c) && ++changed <= 12)
              SDL_Log("[TankLab] column pixel %d,%d was %08x, now %08x", x, y, unsigned(c), unsigned(px[size_t(y) * pw + x]));
          SDL_Log("[TankLab] column after the tanks: %zu of %zu pixels changed", changed, columnBefore.size());
          capture("tanklab_column");
          check(("a rock column in front of a sunk tank's corner hides its wall (" + std::to_string(changed) + " of " +
                 std::to_string(columnBefore.size()) + " pixels changed)").c_str(),
                changed == 0);
        }
        // ours: a check. ZT_TANK_LAB_PATH: path tiles all round the outside
        // of the tanks (issue #101: a path tile behind a back wall was drawn
        // inside the tank below its rail), shot from all four sides
        if (std::getenv("ZT_TANK_LAB_PATH")) {
          WorldMap &wm = world->getMap();
          for (int x = lx - 1; x <= lx + 8; x++)
            for (int y : {ly - 1, ly + 4})
              wm.setPath(x, y, "path");
          for (int y = ly; y <= ly + 3; y++)
            for (int x : {lx - 1, lx + 8})
              wm.setPath(x, y, "path");
          wm.touch();
          world->centreOn(lx + 4.0f, ly + 2.0f);
          for (int r = 0; r < 4; r++) {
            capture(("tanklab_path" + std::to_string(r)).c_str());
            world->rotateView(1);
          }
          // (ZT_TANK_LAB_SAVE=<file.zoo>: saved, shared, for the original to
          // load in the Lab - the same tanks and paths seen in zoo.exe)
          if (const char *save = std::getenv("ZT_TANK_LAB_SAVE"); save && g_hud)
            SDL_Log("[TankLab] saved %s: %d", save,
                    static_cast<int>(SaveGame::save(save, *world, g_sim, *g_hud, g_currentMapPath)));
        }
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
        // ZT_TANK_LAB_OBJ: underwater rock and foliage, a surface plant and a
        // land rock tried in the left tank (the original's rules), then shot
        if (std::getenv("ZT_TANK_LAB_OBJ")) {
          struct Try {
            const char *file;
            float x, y;
          } tries[] = {{"scenery/other/aqrock2.ai", lx + 1.5f, ly + 1.5f},
                       {"scenery/foliage/kelp.ai", lx + 2.5f, ly + 2.5f},
                       {"scenery/foliage/sgrass.ai", lx + 0.5f, ly + 3.5f},
                       {"scenery/foliage/wtrlily.ai", lx + 3.5f, ly + 2.5f},
                       {"scenery/other/lrock1.ai", lx + 2.5f, ly + 1.5f}};
          for (const Try &t : tries) {
            Fences::Fit f = world->objectFit(t.file, t.x, t.y, 4);
            bool put = f == Fences::Fit::Ok && world->objectsMutable().add(t.file, t.x, t.y, 4);
            SDL_Log("[TankLab] %s: fit %d message %d placed %d", t.file, static_cast<int>(f), world->fitMessage,
                    static_cast<int>(put));
          }
          world->reindex();
          // Animals: two hammerheads in it; a zebra refused
          for (int i = 0; i < 20; i++)
            world->getFences().update(1.0f); // (the water up again)
          Animals &an = world->getAnimals();
          int shark = an.typeOfFile("animals/hhshark.ai"), zebra = an.typeOfFile("animals/zebra.ai");
          int s1 = an.adopt(shark, false, lx + 0.5f, ly + 0.5f, world->getMap(), fences);
          SDL_Log("[TankLab] shark 1: %d (message %d)", s1, an.placeMessage);
          for (const Fences::Exhibit &x : fences.exhibits())
            if (x.tank)
              SDL_Log("[TankLab] tank %d wallSub %d floor %d ground %d water %.2f level %.2f subtiles %d", x.id, x.wallSub,
                      x.floorHeight, x.groundHeight, x.water, x.level, fences.waterSubtiles(x));
          int s2 = an.adopt(shark, true, lx + 3.5f, ly + 0.5f, world->getMap(), fences);
          SDL_Log("[TankLab] shark 2: %d (message %d)", s2, an.placeMessage);
          int z1 = an.adopt(zebra, false, lx + 2.5f, ly + 3.5f, world->getMap(), fences);
          SDL_Log("[TankLab] zebra: %d (message %d)", z1, an.placeMessage);
          world->centreOn(lx + 2.0f, ly + 2.0f);
          for (int r = 0; r < 4; r++) {
            capture(("tankobj_rot" + std::to_string(r)).c_str());
            world->rotateView(1);
          }
          // A marine specialist for it, a filter beside it; the sharks hungry,
          // the water dirty: fed, eaten, cleaned
          Staff &st = world->getStaff();
          int trainer = -1;
          for (int r = 1; r < 8 && trainer < 0; r++)
            for (int dx = -r; dx <= r && trainer < 0; dx++)
              trainer = st.hire(st.typeOfFile("staff/trainer.ai"), lx + 2.5f + dx, ly - r + 0.5f, world->getMap(), fences);
          const Fences::Exhibit *tk = nullptr;
          for (const Fences::Exhibit &x : fences.exhibits())
            if (x.tank && x.tiles.count({lx, ly}))
              tk = &x;
          world->update(SDL_GetKeyboardState(nullptr), 0.05f); // (the tank's animals counted)
          int why = tk ? st.assign(trainer, tk->id, fences) : -1;
          SDL_Log("[TankLab] specialist %d assigned: %d", trainer, why);
          bool filter = false;
          for (int fy = ly - 3; fy <= ly + 5 && !filter; fy++)
            for (int fx = lx - 3; fx <= lx + 5 && !filter; fx++) {
              int t = -1;
              if (fences.filterFit(fx, fy, world->getMap(), &t) == Fences::Fit::Ok && tk && t == tk->id)
                filter = fences.placeFilter(fx, fy, world->getMap());
            }
          SDL_Log("[TankLab] filter placed: %d", static_cast<int>(filter));
          // Its sell value (zoo.exe ZTTankFilter::sellValue 0x50c961): 80% of
          // its cost; worn to cDecayedHealth, half that; broken, nothing
          if (filter && !fences.filters().empty()) {
            const int fi = static_cast<int>(fences.filters().size()) - 1;
            Fences::Filter &fl = const_cast<Fences::Filter &>(fences.filters()[fi]);
            const Fences::FilterType &k = fences.filterType(fl.file);
            const float health = fl.health;
            const float full = fences.sellValue(fi);
            fl.health = static_cast<float>(k.decayedHealth);
            const float worn = fences.sellValue(fi);
            fl.health = 0;
            const float broken = fences.sellValue(fi);
            fl.health = health;
            check(("a filter sells for $" + std::to_string(full) + ", worn $" + std::to_string(worn) + ", broken $" +
                   std::to_string(broken) + " (cost $" + std::to_string(k.cost) + ")").c_str(),
                  std::abs(full - k.cost * 0.8f) < 1e-3f && std::abs(worn - k.cost * 0.4f) < 1e-3f && broken == 0);
          }
          for (Animals::Member &m : const_cast<std::vector<Animals::Member> &>(an.members()))
            if (m.exhibit == (tk ? tk->id : -2))
              m.hunger = 80;
          if (tk)
            const_cast<Fences::Exhibit *>(tk)->purity = 55;
          float ateBefore = 0;
          for (const Animals::Member &m : an.members())
            ateBefore += m.hunger;
          for (int f = 0; f < 12; f++) {
            for (int i = 0; i < 50; i++)
              world->update(SDL_GetKeyboardState(nullptr), 0.1f);
            if (const Staff::Member *sm = st.member(trainer))
              SDL_Log("[TankLab] specialist: %s; food %.0f; purity %.0f", st.dutyText(*sm).c_str(),
                      tk ? world->getItems().foodIn(tk->id) : -1.0f, tk ? tk->purity : -1.0f);
            for (const Animals::Member &m : an.members())
              SDL_Log("[TankLab]   %s hunger %.0f anim %s", m.name.c_str(), m.hunger, m.anim.c_str());
            if (const Animals::Member *m = an.member(s1))
              SDL_Log("[TankLab] shark at %.1f,%.1f z %.2f -> %.2f anim %s exhibit %d set %s path %zu hunger %.0f", m->x, m->y,
                      m->swimZ, m->swimTo, m->anim.c_str(), m->exhibit, m->stack.empty() ? "-" : m->stack.back().first.c_str(),
                      m->path.size(), m->hunger);
            capture(("tankswim" + std::to_string(f)).c_str());
          }
          // The filter run down; a maintenance worker services it
          if (filter && !fences.filters().empty()) {
            const_cast<Fences::Filter &>(fences.filters().back()).health = 2;
            int maint = -1;
            for (int r = 1; r < 8 && maint < 0; r++)
              for (int dx = -r; dx <= r && maint < 0; dx++)
                maint = st.hire(st.typeOfFile("staff/maint.ai"), lx + 2.5f + dx, ly + 5 + r + 0.5f, world->getMap(), fences);
            bool serviced = false;
            for (int i = 0; i < 1200 && !serviced; i++) {
              world->update(SDL_GetKeyboardState(nullptr), 0.1f);
              const Staff::Member *mm = st.member(maint);
              if (i % 100 == 0 && mm)
                SDL_Log("[TankLab] maint: %s; filter health %.1f", st.dutyText(*mm).c_str(), fences.filters().back().health);
              serviced = fences.filters().back().health >= fences.filterType().startHealth - 0.01f;
            }
            SDL_Log("[TankLab] filter serviced: %d", static_cast<int>(serviced));
            capture("tankfilter_serviced");
          }
          return 0;
        }
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
          // Zoomed in (1.5x, 2x, 1.37x): no lines on the water
          for (float z : {1.5f, 2.0f, 1.37f, 0.75f, 0.6f}) {
            float saved = world->getRenderer().getCamera().zoom;
            world->getRenderer().getCamera().zoom = z;
            world->centreOn(lx + 2.0f, ly + 2.0f);
            char nm[32];
            std::snprintf(nm, sizeof nm, "tankzoom_%d", static_cast<int>(z * 100));
            capture(nm);
            world->getRenderer().getCamera().zoom = saved;
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
        const int step = unit; // (a wall step: a height unit)
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
        check("the water follows the walls up", std::fabs(mt->level - (mt->wallTop() - 1.0f)) < 0.01f);
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
        // Raised: its hint and up/down cursor only by its top, within 20 px
        // under the rail (zoo.exe 0x4fb5ab) - not half way down its face
        {
          int fx, fy;
          world->pointToWindow(mx, my, (mt->groundHeight + mt->wallTop()) * 0.5f, fx, fy);
          world->mouseMove(fx, fy);
          bool lowNone = world->hoverTip().empty() && !world->overTankWallTop();
          world->pointToWindow(mx, my, mt->wallTop() - 0.25f, fx, fy);
          world->mouseMove(fx, fy);
          check("a raised tank wall's hint and up/down cursor by its rail, not half way down",
                lowNone && world->hoverTip() == rm->getString(31079) && world->overTankWallTop());
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
          // (the bulldozer over the middle of each wall's face: the piece
          // under it glows red)
          {
            const float mid = (gt->groundHeight + gt->wallTop()) * 0.5f;
            const std::pair<float, float> walls[4] = {
                {gx + 0.5f, static_cast<float>(gy)}, {gx + 2.0f, gy + 0.5f}, {gx + 0.5f, gy + 2.0f}, {static_cast<float>(gx), gy + 0.5f}};
            world->setBulldozer(true);
            for (int w = 0; w < 4; w++) {
              int sx, sy;
              world->pointToWindow(walls[w].first, walls[w].second, mid, sx, sy);
              world->mouseMove(sx, sy);
              capture(("glass_bulldoze_" + std::to_string(w)).c_str());
              const Fences::Edge e = world->bulldozeEdge();
              const Fences::Piece *pc = e.x >= 0 ? fences.at(e) : nullptr;
              SDL_Log("[PanelShots] glass wall %d at %.1f,%.1f (window %d,%d): picked %s %d,%d tank %d", w, walls[w].first,
                      walls[w].second, sx, sy, e.x < 0 ? "nothing" : e.alongX ? "x-edge" : "y-edge", e.x, e.y,
                      pc ? pc->tank : -1);
            }
            // (an ordinary fence a tile in front of each of its walls: pointing
            // at it picks the fence, not the tank behind it - the tank's wall
            // always won)
            {
              const int chain = fences.typeIndex("chainlnk");
              const Fences::Edge fronts[4] = {{true, gx, gy - 1}, {false, gx + 3, gy}, {true, gx, gy + 3}, {false, gx - 1, gy}};
              for (int w = 0; w < 4 && chain >= 0; w++) {
                const Fences::Edge fe = fronts[w];
                if (fences.at(fe) || !fences.canPlace(fe, world->getMap()))
                  continue;
                fences.place(fe, chain, 990 + w, world->getMap());
                const float ax = fe.alongX ? fe.x + 0.5f : static_cast<float>(fe.x);
                const float ay = fe.alongX ? static_cast<float>(fe.y) : fe.y + 0.5f;
                const MapTile *t0 = world->getMap().getTile(fe.x, fe.y);
                int sx, sy, cx0, cy0;
                world->pointToWindow(ax, ay, (t0 ? t0->height : 0) + 1.0f, sx, sy);
                // (one behind the tank is hidden by it there: the tank is right)
                world->pointToWindow(gx + 1.0f, gy + 1.0f, (t0 ? t0->height : 0) + 1.0f, cx0, cy0);
                if (sy < cy0) {
                  fences.remove(fe, world->getMap());
                  continue;
                }
                world->mouseMove(sx, sy);
                const Fences::Edge e = world->bulldozeEdge();
                const bool fencePicked = e.x == fe.x && e.y == fe.y && e.alongX == fe.alongX;
                capture(("glass_fence_in_front_" + std::to_string(w)).c_str());
                check(("pointing at a fence beside a tank picks the fence (" + std::to_string(fe.x) + "," +
                       std::to_string(fe.y) + (fe.alongX ? " x" : " y") + "-edge: picked " + std::to_string(e.x) + "," +
                       std::to_string(e.y) + ")").c_str(),
                      fencePicked);
                fences.remove(fe, world->getMap());
              }
            }
            world->setBulldozer(false);
          }
          int id = gt->id;
          while (fences.canAdjustBase(id, 1))
            fences.adjustBase(id, 1);
          for (int i = 0; i < 4; i++)
            fences.adjustWall(id, 1);
          world->getFences().update(20.0f);
          // (raised: the bulldozer over the ground just in front of each front
          // wall - outside the tank, nearer than it - picks nothing of the
          // tank; a back wall was lit from the path in front)
          {
            world->setBulldozer(true);
            const std::pair<float, float> mids[4] = {
                {gx + 1.0f, static_cast<float>(gy)}, {gx + 2.0f, gy + 1.0f}, {gx + 1.0f, gy + 2.0f}, {static_cast<float>(gx), gy + 1.0f}};
            const std::pair<float, float> outward[4] = {{0, -1}, {1, 0}, {0, 1}, {-1, 0}};
            for (int w = 0; w < 4; w++) {
              const float ox = mids[w].first + outward[w].first * 0.75f, oy = mids[w].second + outward[w].second * 0.75f;
              const MapTile *t0 = world->getMap().getTile(static_cast<int>(std::floor(ox)), static_cast<int>(std::floor(oy)));
              int sx, sy, wx0, wy0;
              world->pointToWindow(ox, oy, t0 ? static_cast<float>(t0->height) : 0.0f, sx, sy);
              world->pointToWindow(mids[w].first, mids[w].second, t0 ? static_cast<float>(t0->height) : 0.0f, wx0, wy0);
              if (sy <= wy0)
                continue; // (a back wall: the ground beyond it is behind the tank)
              world->mouseMove(sx, sy);
              const Fences::Edge e = world->bulldozeEdge();
              const Fences::Piece *pc = e.x >= 0 ? fences.at(e) : nullptr;
              capture(("glass_up_in_front_" + std::to_string(w)).c_str());
              check(("the ground in front of a raised tank picks nothing of it (wall " + std::to_string(w) + ": " +
                     (pc && pc->tank == id ? "picked its " + std::to_string(e.x) + "," + std::to_string(e.y) : std::string("not the tank")) +
                     ")").c_str(),
                    !(pc && pc->tank == id));
            }
            world->setBulldozer(false);
          }
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
        // (its state and upkeep as zoo.exe 0x615d28 writes them: "Broken
        // Down" at no health; whole dollars, no cents)
        {
          Fences::Filter &ff = const_cast<Fences::Filter &>(fences.filters().front());
          const float health = ff.health;
          const double upkeep = ff.upkeepTotal;
          ff.health = 0;
          ff.upkeepTotal = 1234.56;
          std::vector<Input> none;
          g_hud->handleInputs(none);
          UiText *state = dynamic_cast<UiText *>(g_hud->getElementById(4913));
          UiText *total = dynamic_cast<UiText *>(g_hud->getElementById(4925));
          const std::string shownState = state ? state->getText() : "", shownTotal = total ? total->getText() : "";
          ff.health = health;
          ff.upkeepTotal = upkeep;
          g_hud->handleInputs(none);
          check(("filter panel: broken shows " + shownState + ", upkeep " + shownTotal).c_str(),
                shownState == rm->getString(4999) && shownTotal == "$1234");
        }
        click(4910);
        capture("filter_panel_finance");
        // Sell: asked first (filter1.ai's cNeedsConfirm), then 80% of its
        // price back on Recycling Benefit (zoo.exe sellObject 0x50a432,
        // ZTTankFilter::sellValue 0x50c961, bookRemovalRefund 0x4f9329)
        {
          double cash = g_sim.cash();
          const double recycling = g_sim.history().back().lines[ZooSim::Recycling];
          const float value = fences.sellValue(0);
          click(4906);
          const bool asked = g_hud->hasDialog() && !fences.filters().empty();
          enter();
          check(("filter Sell asks, then gives $" + std::to_string(std::lround(g_sim.cash() - cash)) +
                 " back on Recycling Benefit (80% of $" + std::to_string(fences.filterType().cost) + ")").c_str(),
                asked && fences.filters().empty() &&
                    std::abs(g_sim.cash() - cash - fences.filterType().cost * Original::sellFactor) < 0.01 &&
                    std::abs(value - fences.filterType().cost * Original::sellFactor) < 0.01f &&
                    std::abs(g_sim.history().back().lines[ZooSim::Recycling] - recycling - value) < 0.01);
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
    handleToolResult(world->mouseDown((bx + bx2) / 2, (by + by2) / 2), rm); handleToolResult(world->mouseUp((bx + bx2) / 2, (by + by2) / 2), rm);
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
      // (the original: the hole stays; the tankgroundback switch: the ground
      // back as it was, level with round it here)
      if (Features::tankGroundBack)
        check("the ground back as it was (tankgroundback)", in && out && in->height == out->height);
      else
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
    // original's timed release (months played from 0: unlock02.scn's
    // SECOND_UNLOCK, 6): in July the Tank Filter is there, first, Black Bar
    // and Glass fifth
    {
      ItemCatalog::get().setMonth(unlockMonthFor(0));
      auto all = ItemCatalog::get().inCategory("fence");
      check("all unlocked: Tank Filter first in January",
            !Features::allUnlocked || (!all.empty() && all[0]->file == "scenery/other/filter1.ai"));
      ItemCatalog::get().setMonth(6);
      auto list = ItemCatalog::get().inCategory("fence");
      check("July: Tank Filter first", !list.empty() && list[0]->file == "scenery/other/filter1.ai");
      check("July: Black Bar and Glass fifth", list.size() > 4 && list[4]->file == "fences/tankwall.ai");
      ItemCatalog::get().setMonth(5);
      auto june = ItemCatalog::get().inCategory("fence");
      check("June: no Tank Filter yet", !june.empty() && june[0]->file != "scenery/other/filter1.ai");
      ItemCatalog::get().setMonth(0);
      auto jan = ItemCatalog::get().inCategory("fence");
      check("January: no Tank Filter", !jan.empty() && jan[0]->file == "fences/tankwal2.ai");
      ItemCatalog::get().setMonth(unlockMonthFor(0));
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
      SDL_SetRenderScale(renderer, 6, 6);
      for (int f = 0; f < 6; f++)
        a->drawAnchored(renderer, 20.0f + f * 36, 40.0f, CompassDirection::SE, nullptr, f);
      for (int f = 0; f < 6; f++)
        a->drawAnchored(renderer, 20.0f + f * 36, 90.0f, CompassDirection::SW, nullptr, f);
      SDL_SetRenderScale(renderer, 1, 1);
      int w = 0, h = 0;
      SDL_GetCurrentRenderOutputSize(renderer, &w, &h);
      SDL_Surface *sf = SDL_CreateSurface(w, h, SDL_PIXELFORMAT_ARGB8888);
      ZT_RenderReadPixels(renderer, nullptr, SDL_PIXELFORMAT_ARGB8888, sf->pixels, sf->pitch);
      SDL_SaveBMP(sf, (outDir + "/seagull_frames.bmp").c_str());
      SDL_DestroySurface(sf);
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
      // Delete on the list does nothing, a row picked or not (zoo.exe's
      // mulstaff_list_pickB 0x61841d sits on the event table 0x6391ec that
      // nothing fires; measured in the running game, #270)
      if (UiListBox *sl = dynamic_cast<UiListBox *>(g_hud->getElementById(4205)); sl && sl->getItemCount() > 0) {
        SDL_Rect r = sl->getScrollBounds();
        Input in{};
        in.type = InputType::POSITIONED;
        in.event = InputEvent::LEFT_CLICK;
        in.position = {r.x + r.w / 2, r.y + 2 + sl->getRowHeight() / 2};
        in.x = in.position.x;
        in.y = in.position.y;
        std::vector<Input> inputs = {in};
        g_hud->handleInputs(inputs);
        const int picked = g_hud->shownStaffId();
        const size_t staffBefore = st.members().size();
        Input del{};
        del.type = InputType::BUTTON;
        del.event = InputEvent::KEY_DOWN;
        del.key = SDLK_DELETE;
        std::vector<Input> keys = {del};
        g_hud->handleInputs(keys);
        const bool quiet = !g_hud->dialogOpen();
        if (g_hud->dialogOpen())
          g_hud->answerDialog(false);
        check(("Delete on the Staff List does nothing (0x61841d never fired, measured; picked " + std::to_string(picked) + ")").c_str(),
              picked >= 0 && quiet && st.member(picked) && st.members().size() == staffBefore && g_hud->isPanelOpen(17));
      }
      // Hire Staff (#270, measured in the running game): a pick shows the
      // name, Salary and Duties and enables the info plaque button; the
      // plaque (splaque.lyt, 33) shows the type's name and TEXT; a right
      // click on the map lets the pick go (personel_Staff_off 0x4e6711: no
      // name or texts, labels hidden, the button greyed, the plaque left
      // up); closing Hire Staff shuts the plaque (0x4e6794); opening it
      // picks the last type again (personel_Panel_show 0x4e6fc2); the
      // button clicked again shuts the plaque (staffPlaque_close 0x4816a9)
      {
        std::vector<Input> none;
        auto hidden = [&](int id) {
          UiElement *e = g_hud->getElementById(id);
          return !e || e->isHidden();
        };
        auto textOf = [&](int id) {
          UiText *t = dynamic_cast<UiText *>(g_hud->getElementById(id));
          return t ? t->getText() : std::string("?");
        };
        g_hud->showPanel(10, "");
        g_hud->handleInputs(none);
        capture("hire_staff");
        UiButton *plaque = dynamic_cast<UiButton *>(g_hud->getElementById(3699));
        const std::string held = g_hud->staffToolFile();
        const int heldType = st.typeOfFile(held);
        const std::string heldName = heldType >= 0 ? rm->getString(st.types()[heldType].nameId) : "";
        const bool picked = !held.empty() && !hidden(3610) && !hidden(3612) && !hidden(3620) && plaque &&
                            !plaque->isDisabled() && !textOf(3611).empty();
        click(3699);
        capture("hire_staff_plaque");
        const bool plaqueUp = g_hud->isPanelOpen(33) && plaque && plaque->isToggledOn() && textOf(40001) == heldName &&
                              textOf(40003).size() > 40;
        g_hud->dropPicks();
        g_hud->handleInputs(none);
        capture("hire_staff_let_go");
        const bool letGo = g_hud->staffToolFile().empty() && hidden(3610) && hidden(3612) && hidden(3620) &&
                           textOf(3611).empty() && textOf(3613).empty() && plaque->isDisabled() && g_hud->isPanelOpen(33);
        g_hud->showPanel(0, "");
        g_hud->handleInputs(none);
        const bool plaqueShut = !g_hud->isPanelOpen(33) && !plaque->isToggledOn();
        g_hud->showPanel(10, "");
        g_hud->handleInputs(none);
        capture("hire_staff_again");
        const bool pickedAgain = g_hud->staffToolFile() == held && !hidden(3610);
        click(3699);
        const bool on2 = g_hud->isPanelOpen(33);
        capture("hire_staff_plaque2");
        click(3699);
        const bool off2 = !g_hud->isPanelOpen(33) && !plaque->isToggledOn();
        check(("Hire Staff: pick, info plaque, right click lets go, close shuts the plaque, reopen picks again (#270; " + held + " \"" +
               textOf(40001) + "\" " + std::to_string(picked) + std::to_string(plaqueUp) + std::to_string(letGo) +
               std::to_string(plaqueShut) + std::to_string(pickedAgain) + std::to_string(on2) + std::to_string(off2) + ")")
                  .c_str(),
              picked && plaqueUp && letGo && plaqueShut && pickedAgain && on2 && off2);
        g_hud->showPanel(0, "");
        g_hud->handleInputs(none);
      }
      g_hud->showPanel(0, "");
      g_hud->showStaff(ids[1]);
      capture("staff_panel_keeper");
      // Assign, a toggle (zoo.exe 0x471c30, 0x471ebd): on, the map picks the
      // keeper's exhibits and Job Assignment comes up; off, it stops; Remove
      // All turns it off first (0x6185d6)
      {
        UiButton *assign = dynamic_cast<UiButton *>(g_hud->getElementById(3578));
        click(3578);
        UiButton *job = dynamic_cast<UiButton *>(g_hud->getElementById(3564));
        const bool on = world->isAssigning() && assign && assign->isToggledOn() && job && job->isToggledOn();
        click(3578);
        const bool off = !world->isAssigning() && assign && !assign->isToggledOn();
        click(3578);
        capture("staff_assign_on");
        click(3580);
        const bool cleared = !world->isAssigning() && assign && !assign->isToggledOn();
        check("Assign toggles picking exhibits, brings up Job Assignment; Remove All ends it",
              on && off && cleared);
      }
      // A keeper's exhibit picked in Job Assignment (#252, zoo.exe
      // infostf2_AssignList_click 0x61861b): Assign off, Remove enabled (off
      // with no row picked, 0x47108a), the Exhibit List opened with that
      // exhibit picked, Staff Information still open
      {
        int pen = -1;
        for (const Fences::Exhibit &e : fn.exhibits())
          if (e.named && !e.tank && !e.show && pen < 0)
            for (const auto &[edge, piece] : fn.pieces())
              if (piece.gate && fn.exhibitOf(edge) == e.id)
                pen = e.id;
        Staff::Member *keeper = st.member(ids[1]);
        if (pen >= 0 && keeper) {
          keeper->exhibits = {pen};
          g_hud->showPanel(31, "");
          g_hud->showStaff(ids[1]);
          click(3564);
          g_hud->refreshStaff();
          capture("staff_keeper_exhibits");
          UiButton *remove = dynamic_cast<UiButton *>(g_hud->getElementById(3579));
          const bool offFirst = remove && remove->isDisabled();
          bool picked = false;
          if (UiListBox *l = dynamic_cast<UiListBox *>(g_hud->getElementById(3569))) {
            SDL_Rect r = l->getScrollBounds(); // (as last drawn)
            Input in{};
            in.type = InputType::POSITIONED;
            in.event = InputEvent::LEFT_CLICK;
            in.position = {r.x + r.w / 2, r.y + 2 + l->getRowHeight() / 2};
            in.x = in.position.x;
            in.y = in.position.y;
            std::vector<Input> inputs = {in};
            g_hud->handleInputs(inputs);
            picked = l->getSelectedIndex() == 0;
          }
          g_hud->refreshStaff();
          capture("staff_keeper_exhibit_picked");
          UiButton *assign = dynamic_cast<UiButton *>(g_hud->getElementById(3578));
          check(("keeper's exhibit picked: Exhibit List open on it, Remove enabled, Assign off (0x61861b; exhibit " +
                 std::to_string(pen) + ", list on " + std::to_string(g_hud->shownExhibitId()) + ", row picked " +
                 std::to_string(picked) + ", Remove off before " + std::to_string(offFirst) + ")").c_str(),
                offFirst && picked && g_hud->isPanelOpen(30) && g_hud->shownExhibitId() == pen && g_hud->isPanelOpen(17) &&
                    !g_hud->isPanelOpen(31) && remove && !remove->isDisabled() && assign && !assign->isToggledOn() &&
                    !world->isAssigning());
          // (the view to the gate's tile, habitat +0x8c - measured: the
          // gate's outside tile, #266)
          {
            float vx = -1, vy = -1;
            int gx = -1, gy = -1;
            for (const auto &[edge, piece] : fn.pieces())
              if (piece.gate && fn.exhibitOf(edge) == pen) {
                const int ax = edge.alongX ? edge.x : edge.x - 1, ay = edge.alongX ? edge.y - 1 : edge.y;
                const bool aInside = fn.exhibitAt(ax, ay) == pen;
                gx = aInside ? edge.x : ax;
                gy = aInside ? edge.y : ay;
                break;
              }
            const bool centred = world->viewCentre(vx, vy) && std::abs(vx - (gx + 0.5f)) < 0.75f &&
                                 std::abs(vy - (gy + 0.5f)) < 0.75f && fn.exhibitAt(gx, gy) != pen;
            check(("keeper's exhibit picked: the view on the gate's outside tile (habitat +0x8c, measured; tile " +
                   std::to_string(gx) + "," + std::to_string(gy) + ", view " + std::to_string(vx) + "," +
                   std::to_string(vy) + ")").c_str(),
                  centred);
          }
          // (#266: a keeper's pick only picks the exhibit, 0x61861b -> 0x58b5ce;
          // the Exhibit List's own pick selects its gate - 0x599ae7 ->
          // 0x492ca3 -> ui_main_4b2322(gate, 1), the arrow over it, measured
          // - closing what was selected's information; the list shut, it's
          // deselected - mulhab_Panel_hide 0x4682f1 -> 0x4ef75b, measured)
          {
            const bool keeperNone = g_hud->selectedGateExhibit() < 0;
            bool listPicked = false;
            if (UiListBox *list = dynamic_cast<UiListBox *>(g_hud->getElementById(4305))) {
              list->setSelectedIndex(-1);
              g_hud->refreshExhibits();
              for (int row = 0; row < static_cast<int>(list->getItemCount()) && !listPicked; row++) {
                list->setSelectedIndex(row);
                g_hud->refreshExhibits();
                listPicked = g_hud->shownExhibitId() == pen;
              }
            }
            const int selected = g_hud->selectedGateExhibit();
            const bool infoShut = !g_hud->isPanelOpen(17);
            fn.selectedGate = selected;
            capture("exhibit_list_gate_selected");
            g_hud->setPanelOpen(30, false);
            const int afterHide = g_hud->selectedGateExhibit();
            fn.selectedGate = -1;
            check(("Exhibit List: its pick selects the exhibit's gate (not a keeper's pick), Staff Information shut; "
                   "shut, the gate deselected (0x599ae7, 0x4682f1; keeper's " + std::to_string(keeperNone) + ", list's " +
                   std::to_string(selected) + " of " + std::to_string(pen) + ", after " + std::to_string(afterHide) + ")").c_str(),
                  keeperNone && listPicked && selected == pen && infoShut && afterHide < 0);
          }
          keeper = st.member(ids[1]);
          if (keeper)
            keeper->exhibits.clear();
        } else {
          check("keeper's exhibit picked: an exhibit with a gate to give the keeper", false);
        }
      }
      // #290, the Exhibit List's pages (zoo.exe 0x58b5ce, 0x468b9d,
      // 0x58b5f2, exhibitListRefresh 0x5b108b), as measured 2026-11-18 in
      // Play original: two tanks laid by a land exhibit for it
      {
        WorldMap &wm = world->getMap();
        int land = -1;
        for (const Fences::Exhibit &e : fn.exhibits())
          if (e.named && !e.tank && land < 0)
            land = e.id;
        int tx = -1, ty = -1;
        for (int y = 4; y < wm.getHeight() - 8 && tx < 0; ++y)
          for (int x = 4; x < wm.getWidth() - 14 && tx < 0; ++x) {
            bool ok = true;
            for (int yy = y - 1; yy <= y + 5 && ok; ++yy)
              for (int xx = x - 1; xx <= x + 11 && ok; ++xx)
                ok = fn.exhibitAt(xx, yy) < 0 && fn.insideZoo(xx, yy) && !wm.isPath(xx, yy);
            for (const auto &o : world->getObjects().objects())
              ok = ok && !(o.x > x - 2 && o.x < x + 13 && o.y > y - 2 && o.y < y + 7);
            if (ok) {
              tx = x;
              ty = y;
            }
          }
        const int atl = fn.typeIndex("atltank");
        if (land >= 0 && tx >= 0 && atl >= 0) {
          int drag = 9000;
          auto tank = [&](int x, int y, const char *name) {
            for (int k = 0; k < 4; ++k) {
              fn.place({true, x + k, y}, atl, drag, wm);
              fn.place({true, x + k, y + 4}, atl, drag + 1, wm);
              fn.place({false, x, y + k}, atl, drag + 2, wm);
              fn.place({false, x + 4, y + k}, atl, drag + 3, wm);
            }
            drag += 4;
            for (int id : fn.updateExhibits(wm, 1, 0, 1))
              fn.finishExhibit(id, name, wm);
            return fn.exhibitAt(x + 1, y + 1);
          };
          const int tankA = tank(tx, ty, "Pages tank A"), tankB = tank(tx + 6, ty, "Pages tank B");
          world->reindex();
          // (as a zoo just loaded: no tank picked yet this game)
          g_hud->restartExhibitPages();
          g_hud->setPanelOpen(30, true);
          g_hud->refreshExhibits();
          UiListBox *list = dynamic_cast<UiListBox *>(g_hud->getElementById(4305));
          auto pick = [&](int want) {
            if (list) {
              list->setSelectedIndex(g_hud->exhibitRow(want));
              g_hud->refreshExhibits();
            }
          };
          auto tab = [&](int id) {
            if (UiButton *b = dynamic_cast<UiButton *>(g_hud->exhibitElement(id)))
              b->choose();
            g_hud->syncExhibitTabs();
          };
          auto on = [&](int id) {
            UiButton *b = dynamic_cast<UiButton *>(g_hud->exhibitElement(id));
            return b && b->isToggledOn();
          };
          auto shown = [&](int id) {
            UiElement *e = g_hud->exhibitElement(id);
            return e && !e->isHidden();
          };
          auto state = [&]() {
            std::string s;
            for (int p : {700, 701, 702, 703, 704})
              s += shown(p) ? "1" : "0";
            s += "/";
            for (int t : {4310, 4311, 4312, 4313, 4365})
              s += on(t) ? "1" : "0";
            return s;
          };
          // Opened, nothing picked: no page
          const bool openEmpty = !shown(700) && !shown(704);
          const std::string openState = state();
          pick(tankA);
          const bool firstTank = on(4365) && shown(704) && !shown(700);
          const std::string firstState = state() + " row " + std::to_string(g_hud->exhibitRow(tankA)) + " shown " +
                                         std::to_string(g_hud->shownExhibitId()) + " of " + std::to_string(tankA);
          tab(4311);
          pick(tankB);
          const bool animalsKept = on(4311) && shown(701);
          tab(4365);
          pick(tankA);
          const bool tankKept = on(4365) && shown(704) && !shown(700);
          pick(land);
          UiButton *tankTab = dynamic_cast<UiButton *>(g_hud->exhibitElement(4365));
          const bool landStatus = on(4310) && shown(700) && !shown(704) && tankTab && tankTab->isDisabled();
          tab(4312);
          pick(tankB);
          const bool thoughtsKept = on(4312) && shown(702) && !(tankTab && tankTab->isDisabled());
          g_hud->setPanelOpen(30, false);
          g_hud->setPanelOpen(30, true);
          g_hud->refreshExhibits();
          const bool reopened = g_hud->shownExhibitId() < 0 && on(4312) && !shown(700) && !shown(704);
          capture("exhibit_list_pages");
          g_hud->setPanelOpen(30, false);
          for (int id : {tankA, tankB})
            if (fn.exhibit(id))
              fn.drainExhibit(id);
          for (int x0 : {tx, tx + 6})
            for (int k = 0; k < 4; ++k) {
              fn.remove({true, x0 + k, ty}, wm);
              fn.remove({true, x0 + k, ty + 4}, wm);
              fn.remove({false, x0, ty + k}, wm);
              fn.remove({false, x0 + 4, ty + k}, wm);
            }
          fn.updateExhibits(wm, 1, 0, 1);
          world->reindex();
          check(("#290: Exhibit List pages as the original (0x58b5ce, 0x5b108b; open " + std::to_string(openEmpty) +
                 ", first tank " + std::to_string(firstTank) + ", animals kept " + std::to_string(animalsKept) +
                 ", tank kept " + std::to_string(tankKept) + ", land status " + std::to_string(landStatus) +
                 ", thoughts kept " + std::to_string(thoughtsKept) + ", reopened " + std::to_string(reopened) + "; pages/tabs at open " + openState + ", first tank " + firstState + ")")
                    .c_str(),
                openEmpty && firstTank && animalsKept && tankKept && landStatus && thoughtsKept && reopened);
        } else {
          check("#290: room for two tanks by a land exhibit", false);
        }
      }
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
      // (ours: the view back on the fence - the Exhibit List checks above
      // left it on their tanks)
      world->centreOn(worn.x + 0.5f, worn.y + 0.5f);
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
        SDL_SetRenderScale(renderer, 2, 2);
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
        SDL_SetRenderScale(renderer, 1, 1);
        int w = 0, h = 0;
        SDL_GetCurrentRenderOutputSize(renderer, &w, &h);
        SDL_Surface *sf = SDL_CreateSurface(w, h, SDL_PIXELFORMAT_ARGB8888);
        ZT_RenderReadPixels(renderer, nullptr, SDL_PIXELFORMAT_ARGB8888, sf->pixels, sf->pitch);
        SDL_SaveBMP(sf, (outDir + "/staff_sheet.bmp").c_str());
        SDL_DestroySurface(sf);
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
          // As the original: over the wall at the gate, under the water for
          // the food (subfeed) and the sweep, then up and out on the bank
          bool shot = false, climbedIn = false, under = false, fedUnder = false;
          auto inTank = [&] {
            const Staff::Member *ms = st.member(trainer);
            return ms && (ms->tank >= 0 || ms->water != 0);
          };
          for (int i = 0; i < 4000 && te && (te->purity < 100.0f || world->getItems().foodIn(te->id) <= 0 || inTank()); i++) {
            st.update(0.1f, map, fn);
            const Staff::Member *ms = st.member(trainer);
            if (!ms)
              break;
            climbedIn = climbedIn || (ms->climbing && ms->anim == "climb");
            under = under || (ms->water == 3 && fn.exhibitAt(static_cast<int>(std::floor(ms->x)),
                                                              static_cast<int>(std::floor(ms->y))) == te->id);
            fedUnder = fedUnder || (ms->water == 3 && ms->anim == "subfeed");
            if (!shot && ms->dutyId == 10327 && ms->water == 3) {
              shot = true;
              world->centreOnStaff(trainer);
              capture("marine_cleaning");
            }
          }
          check("marine specialist climbs into the tank and dives (water state 3)", climbedIn && under);
          check("marine specialist feeds under the water (bFeedTank: subfeed)", fedUnder);
          {
            const Staff::Member *ms = st.member(trainer);
            check("marine specialist climbs out onto the bank afterwards",
                  ms && ms->tank < 0 && ms->water == 0 && fn.exhibitAt(static_cast<int>(std::floor(ms->x)),
                                                                      static_cast<int>(std::floor(ms->y))) < 0);
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
        // Its own panel (#252: drt.lyt 1400, zoo.exe 0x5b66b1 -> ui_drt_48cd7a;
        // drt_Panel_show 0x48ccc1, 0x48c87d): name, the helicopter's salary,
        // duty, recoveries
        if (h) {
          g_hud->showDrt(at);
          g_hud->refreshStaff();
          capture("staff_drt_panel");
          auto textOf = [&](int id) {
            UiText *t = dynamic_cast<UiText *>(g_hud->getElementById(id));
            return t ? t->getText() : std::string("?");
          };
          check(("DRT base panel: \"" + textOf(1403) + "\", " + textOf(1407) + ", \"" + textOf(1411) + "\", recoveries " +
                 textOf(1409)).c_str(),
                g_hud->isPanelOpen(1400) && !g_hud->isPanelOpen(17) && g_hud->shownDrtId() == at && textOf(1403) == h->name &&
                    textOf(1407) == "$2000" && textOf(1409) == "0" && textOf(1411) == st.dutyText(*h));
          // (TrackDRT shown, its helicopter drawn in it - measured, #266:
          // drt_Panel_show 0x48ccc1 -> 0x4ee2ba)
          {
            UiElement *track = g_hud->getElementById(1405);
            const bool art = st.art(*h, h->anim.empty() ? "idle" : h->anim) != nullptr;
            check("DRT base panel: TrackDRT shown with its helicopter in it (0x48ccc1, 0x4ee2ba)",
                  track && !track->isHidden() && art);
          }
          // The helicopter clicked in flight (#286; measured: 2026-11-18, the
          // Lab's Play original, a DRT base hired with a Spinosaurus loose,
          // the game paused with the helicopter over the zoo and clicked:
          // the DRT panel, "Recovery Building" with the base's name, as for
          // the base - not Staff Information). Picked at its own point on
          // the screen, through the map click's handling
          g_hud->showPanel(0, "");
          g_hud->setPanelOpen(1400, false);
          if (Staff::Member *hm = st.member(at)) {
            hm->heliStage = Staff::HeliStage::Return;
            hm->anim = "fly";
            hm->heliX = hm->x + 4.5f;
            hm->heliY = hm->y + 1.5f;
            hm->heliZ += 6.0f;
            float sx = 0, sy = 0, depth = 0;
            world->getRenderer().worldToScreenF(hm->heliX, hm->heliY, hm->heliZ, sx, sy, depth);
            World::ToolResult click;
            click.staff = st.pick(sx, sy - 8.0f, world->getRenderer(), map);
            handleToolResult(click, rm);
            check("#286: the DRT helicopter clicked in flight opens the DRT panel, as the original (measured; 0x4b2322)",
                  click.staff == at && g_hud->isPanelOpen(1400) && g_hud->shownDrtId() == at && !g_hud->isPanelOpen(17));
            if ((hm = st.member(at))) {
              hm->heliStage = Staff::HeliStage::Parked;
              hm->anim = "idle";
            }
          }
          g_hud->showPanel(0, "");
          g_hud->setPanelOpen(1400, false);
          g_hud->refreshStaff();
        }
        // (let go again: its pad blocks the ground under it, and since it
        // lands on grid-aligned level ground it took the flat strip the
        // walkway and terraform tests below need)
        if (h) {
          st.fire(at);
          world->reindex();
        }
        // Hire Staff's rotate arrows (#284, zoo.exe personel_Staff_click
        // 0x4e69b8: the DRT base's pick shows and enables them, 0x4e58f9;
        // any other hides and greys them, 0x4e65ab); a click turns the base
        // held a quarter (0xe5d_click 0x618883: 0x63e46c on one, the zoo
        // view's +0x370), which the staff tool takes: turned east or west,
        // the base's footprint sides swap and it blocks the ground so
        {
          std::vector<Input> none;
          auto hidden = [&](int id) {
            UiElement *e = g_hud->getElementById(id);
            return !e || e->isHidden();
          };
          auto disabled = [&](int id) {
            UiElement *e = g_hud->getElementById(id);
            return !e || e->isDisabled();
          };
          g_hud->showPanel(10, "");
          g_hud->handleInputs(none);
          const bool picked = g_hud->selectBuyItem(10, "scenery/building/helibase.ai");
          const bool arrows = picked && !hidden(3676) && !hidden(3677) && !disabled(3676) && !disabled(3677);
          const int facing0 = g_hud->staffToolFacing();
          capture("hire_staff_drt"); // (drawn, so the arrows can be clicked)
          click(3677);
          const int facing1 = g_hud->staffToolFacing();
          capture("hire_staff_drt_turned");
          world->setStaffTool(drt, facing0);
          int cx = 0, cy = 0;
          world->pointToWindow(22.5f, 32.5f, 0, cx, cy);
          world->mouseMove(cx, cy);
          const int world0 = st.previewFacing;
          world->setStaffTool(drt, facing1);
          world->mouseMove(cx + 1, cy);
          const int world1 = st.previewFacing;
          world->setStaffTool(-1);
          click(3676);
          const bool back = g_hud->staffToolFacing() == facing0;
          g_hud->selectBuyItem(10, "staff/keeper.ai");
          const bool keeperHides = hidden(3676) && hidden(3677) && disabled(3676) && disabled(3677);
          g_hud->showPanel(0, "");
          g_hud->handleInputs(none);
          // A base turned west: its footprint swapped, the ground blocked so
          const auto foot0 = st.baseFoot(drt, 0), foot6 = st.baseFoot(drt, 6);
          int turned = -1;
          for (int r = 2; r < 30 && turned < 0; r++)
            for (int dy = -r; dy <= r && turned < 0; dy++)
              for (int dx = -r; dx <= r && turned < 0; dx++) {
                float x = 22.0f + dx, y = 32.0f + dy;
                if (st.canPlaceType(drt, x, y, map, fn, 6) == Fences::Fit::Ok)
                  turned = st.hire(drt, x, y, map, fn, 6);
              }
          const Staff::Member *b = st.member(turned);
          const bool blocks = b && st.canPlace(b->x, b->y + foot6.second * 0.5f - 0.25f, map, fn) != Fences::Fit::Ok;
          check(("Hire Staff: the DRT base's rotate arrows turn it; other staff have none; turned west its footprint swaps (#284; " +
                 std::to_string(arrows) + " " + std::to_string(facing0) + ">" + std::to_string(facing1) + " world " +
                 std::to_string(world0) + ">" + std::to_string(world1) + " " + std::to_string(back) +
                 std::to_string(keeperHides) + " foot " + std::to_string(foot6.first) + "x" + std::to_string(foot6.second) +
                 " facing " + std::to_string(b ? b->baseFacing : -1) + " " + std::to_string(blocks) + ")")
                    .c_str(),
                arrows && facing1 == (facing0 + 1) % 4 && world0 != world1 && ((world1 - world0) & 3) == 2 && back &&
                    keeperHides && foot6.first == foot0.second && foot6.second == foot0.first && b && b->baseFacing == 6 &&
                    blocks);
          if (b) {
            world->centreOn(b->x, b->y);
            capture("staff_drt_turned");
            st.fire(turned);
            world->reindex();
          }
          // (and one put down unturned, as Hire Staff opens: its icons' SE)
          int fresh = -1;
          for (int r = 2; r < 30 && fresh < 0; r++)
            for (int dy = -r; dy <= r && fresh < 0; dy++)
              for (int dx = -r; dx <= r && fresh < 0; dx++) {
                float x = 22.0f + dx, y = 32.0f + dy;
                if (st.canPlaceType(drt, x, y, map, fn, world0) == Fences::Fit::Ok)
                  fresh = st.hire(drt, x, y, map, fn, world0);
              }
          if (const Staff::Member *f = st.member(fresh)) {
            world->centreOn(f->x, f->y);
            capture("staff_drt_fresh");
            st.fire(fresh);
            world->reindex();
          }
        }
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
      // A right click mid-drag takes the drag back and keeps the tool; one
      // with no drag lets the tool go (measured in the original, 2026-11-11)
      {
        const double before = g_sim.cash();
        at(x0, y0 + 2, px, py);
        world->mouseMove(px, py);
        handleToolResult(world->mouseDown(px, py), rm);
        for (int i = 1; i <= 3; i++) {
          at(x0 + i, y0 + 2, px, py);
          world->mouseMove(px, py);
        }
        const bool took = world->cancelTool();
        handleToolResult(world->mouseUp(px, py), rm);
        int stray = 0;
        for (int i = 0; i <= 3; i++)
          stray += map.isPath(x0 + i, y0 + 2) ? 1 : 0;
        check("a right click mid path drag takes it back: nothing laid, nothing spent",
              took && stray == 0 && std::lround(before - g_sim.cash()) == 0);
        check("a right click with no drag lets the tool go", !world->cancelTool());
      }
      // The bulldozer out and no drag: a right click puts it away (zoo.exe
      // 0x509870 with no drag: uiTurnOff 0x404 - #265; mid-drag it's kept:
      // the finance checks)
      if (UiButton *clear = dynamic_cast<UiButton *>(g_hud->getElementById(1028))) {
        clear->setToggledOn(true);
        world->setBulldozer(true);
        const bool on = g_hud->bulldozerOn() && world->isBulldozer();
        rightClickOnMap();
        check("a right click with the bulldozer out and no drag puts it away (0x509870)",
              g_world == world && on && !g_hud->bulldozerOn() && !world->isBulldozer());
      }
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
          handleToolResult(world->mouseDown(px, py), rm); handleToolResult(world->mouseUp(px, py), rm);
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
      handleToolResult(world->mouseDown(px, py), rm); handleToolResult(world->mouseUp(px, py), rm);
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
          std::string pressed;
          auto drag = [&](int x0, int y0, int x1, int y1) {
            int px, py;
            // (the press on the tile's own top, as every step after it: at()
            // averages two of its corners, and on the raised block's edge the
            // outer one takes the ground's height beyond it - the point fell
            // half-way down the block's side, and whether that picked the
            // block or its neighbour hung on the window's size and rounding:
            // pppppp12ppp- on some machines)
            {
              const MapTile *t0 = map.getTile(x0, y0);
              world->pointToWindow(x0 + 0.5f, y0 + 0.5f, t0 ? (float)t0->cornerHeight[0] : 0.0f, px, py);
            }
            world->mouseMove(px, py);
            {
              // (where the press landed, in the checks' own words: "pressed 11,2")
              float wx = 0, wy = 0;
              world->groundAt(px, py, wx, wy);
              pressed = "pressed " + std::to_string(static_cast<int>(std::floor(wx)) - sx0) + "," +
                        std::to_string(static_cast<int>(std::floor(wy)) - sy0);
            }
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
          check(("and drawn off it, stairs down (" + down + ", " + pressed + ")").c_str(), down == "pppppp12pppp");
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
  // (the funding a day, as zoo.exe 0x470d0f shows it: min's 400 a month /
  // 30, cut - "$13 min")
  check(("research funding shown a day, as the original (" + textOf(4006) + ")").c_str(),
        textOf(4006) == "$13 min");
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
    Audio::halt(-1);
    Sound::get().play("sounds/uiclick");
    check("sounds play (uiclick.wav)", Audio::playing(-1));
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
      // (measured: choosing a filter starts Adopt Animals' list at its first
      // animal again, a pick made under the one before not kept)
      {
        g_hud->setFilter(3, 0);
        const std::string first = g_hud->selectedBuyItem(3);
        std::string other;
        for (const CatalogItem *c : ItemCatalog::get().inCategory("animals"))
          if (c->file != first && c->expansion == 0 && other.empty())
            other = c->file;
        const bool picked = !other.empty() && g_hud->selectBuyItem(3, other);
        g_hud->setFilter(3, 1);
        g_hud->setFilter(3, 0);
        check("Adopt Animals: choosing a filter starts its list at the first animal again",
              picked && !first.empty() && g_hud->selectedBuyItem(3) == first);
      }
      g_hud->selectBuyItem(3, "animals/zebra.ai");
      click(2000);
      bool female = true;
      std::string pickedFile = g_hud->animalToolFile(female);
      check("the male button takes the zebra", pickedFile == "animals/zebra.ai" && !female);
      // Adopt Animals' info plaque (#283, zoo.exe buya_InfoPlaque_click
      // 0x4833e3, 0x4834cc): plaque.lyt (27) for the animal held - its name,
      // its TEXT, no era or show star for the zebra; a pick while it's up
      // shows that one (buya_Animals_click 0x4e2cf8) - the orca's show star,
      // the allosaurus' era (the panel's too, 0x4e2f17), none for the
      // jellyfish (5403); the carer's
      // button greyed; another tab shuts it and lets the button go
      // (adoptTabs_off 0x4e3867), the button again shuts it, and so does
      // closing Adopt (buya_Animals_hide 0x4e2ba6); nothing held: greyed
      {
        std::vector<Input> none;
        auto hidden = [&](int id) {
          UiElement *e = g_hud->getElementById(id);
          return !e || e->isHidden();
        };
        auto textOf = [&](int id) {
          UiText *t = dynamic_cast<UiText *>(g_hud->getElementById(id));
          return t ? t->getText() : std::string("?");
        };
        UiButton *info = dynamic_cast<UiButton *>(g_hud->getElementById(2050));
        UiScrollingRegion *grid = dynamic_cast<UiScrollingRegion *>(g_hud->getElementById(2019));
        auto pick = [&](const char *file) {
          if (!grid || !g_hud->selectBuyItem(3, file))
            return false;
          if (grid->onSelect)
            grid->onSelect(grid->getSelected());
          g_hud->handleInputs(none);
          return true;
        };
        const CatalogItem *zebraItem = ItemCatalog::get().find("animals/zebra.ai");
        const bool enabled = info && !info->isDisabled() && !info->isToggledOn();
        click(2050);
        g_hud->handleInputs(none);
        capture("adopt_plaque");
        UiButton *carer = dynamic_cast<UiButton *>(g_hud->getElementById(25011));
        const bool up = g_hud->isPanelOpen(27) && info->isToggledOn() && zebraItem && textOf(25001) == zebraItem->name &&
                        textOf(25003).size() > 40 && hidden(25055) && hidden(25098) && carer && carer->isDisabled() &&
                        hidden(2082);
        std::string follow;
        if (pick("animals/orca.ai"))
          follow += g_hud->adoptPlaqueShown() == "animals/orca.ai" && !hidden(25098) && !hidden(25099) ? "o" : "O";
        if (pick("animals/allo.ai"))
          follow += g_hud->adoptPlaqueShown() == "animals/allo.ai" && !hidden(25055) && !textOf(25055).empty() &&
                            !hidden(2082) && textOf(2082) == textOf(25055) ? "a" : "A";
        if (pick("animals/jellyfsh.ai"))
          follow += g_hud->adoptPlaqueShown() == "animals/jellyfsh.ai" && hidden(25098) ? "j" : "J";
        capture("adopt_plaque_follow");
        const bool followed = follow.find_first_of("OAJ") == std::string::npos && g_hud->isPanelOpen(27);
        g_hud->showPanel(3, "shelters");
        g_hud->handleInputs(none);
        const bool tabShut = !g_hud->isPanelOpen(27) && !info->isToggledOn();
        g_hud->showPanel(3, "animals");
        pick("animals/zebra.ai");
        click(2050);
        g_hud->handleInputs(none);
        const bool on2 = g_hud->isPanelOpen(27) && g_hud->adoptPlaqueShown() == "animals/zebra.ai";
        click(2050);
        g_hud->handleInputs(none);
        const bool off2 = !g_hud->isPanelOpen(27) && !info->isToggledOn();
        click(2050);
        g_hud->showPanel(0, "");
        g_hud->handleInputs(none);
        const bool closeShut = !g_hud->isPanelOpen(27);
        g_hud->showPanel(3, "animals");
        g_hud->dropPicks();
        g_hud->handleInputs(none);
        const bool greyed = info->isDisabled() && g_hud->animalToolFile(female).empty();
        check(("Adopt Animals: the info plaque for the animal held, follows picks, shut by a tab, the button and closing (#283; " +
               std::to_string(enabled) + std::to_string(up) + follow + std::to_string(tabShut) + std::to_string(on2) +
               std::to_string(off2) + std::to_string(closeShut) + std::to_string(greyed) + ")")
                  .c_str(),
              enabled && up && followed && tabShut && on2 && off2 && closeShut && greyed);
        // The tabs' titles as their clicks set them: Show Objects 2077, not
        // its helpid 2076 (zoo.exe 0x81c_click 0x4e6414; measured: "Show
        // Objects"); Shelters 6207 (buya_SheltersTab_click 0x4e60e8)
        if (g_hud->getElementById(2076)) {
          g_hud->showPanel(3, "showtoys");
          const std::string show = textOf(2023);
          g_hud->showPanel(3, "shelters");
          const std::string shelters = textOf(2023);
          check(("Adopt Animals: the tabs' titles (\"" + show + "\", \"" + shelters + "\")").c_str(),
                show == rm->getString(2077) && shelters == rm->getString(6207));
        }
        // The content filter is one choice for every panel (zoo.exe
        // 0x5974_click 0x4e4c4d -> ui_buya_4f06a5: 0x638d4c): Dinosaur Digs
        // picked in Adopt leaves the zookeeper out of Hire Staff too, All
        // brings it back
        {
          g_hud->showPanel(3, "animals");
          g_hud->setFilter(3, 2);
          const bool dinoOnly = !g_hud->selectBuyItem(10, "staff/keeper.ai");
          g_hud->setFilter(3, 0);
          const bool all = g_hud->selectBuyItem(10, "staff/keeper.ai");
          check("the content filter is shared by the panels (0x4e4c4d)", dinoOnly && all);
        }
        g_hud->showPanel(3, "animals");
        g_hud->selectBuyItem(3, "animals/zebra.ai");
        click(2000);
        pickedFile = g_hud->animalToolFile(female);
      }
      // The panel's cost as the original's (zoo.exe 0x4e3285 -> 0x40ed88,
      // its grouping off): whole dollars without a thousands comma, "$1000"
      {
        const CatalogItem *dear = nullptr;
        for (const CatalogItem &it : ItemCatalog::get().all())
          if (!dear && it.file.rfind("animals/", 0) == 0 && it.cost >= 1000 &&
              g_hud->selectBuyItem(3, it.file))
            dear = &it;
        UiText *cost = dynamic_cast<UiText *>(g_hud->getElementById(2014));
        std::string shown = cost ? cost->getText() : "";
        check(("an animal's cost shown without a thousands comma (" + shown + ")").c_str(),
              dear && shown == "$" + std::to_string(dear->cost));
        g_hud->selectBuyItem(3, "animals/zebra.ai");
        click(2000);
        // (a tab keeps its pick: Shelters and back to Animals, the zebra
        // still picked - zoo.exe adoptListFill 0x4e334b)
        g_hud->showPanel(3, "shelters");
        g_hud->showPanel(3, "animals");
        const CatalogItem *zebraItem = ItemCatalog::get().find("animals/zebra.ai");
        UiText *picked = dynamic_cast<UiText *>(g_hud->getElementById(2043));
        check(("a tab picks its last pick again (" + (picked ? picked->getText() : std::string("?")) + ")").c_str(),
              zebraItem && picked && picked->getText() == zebraItem->name);
        g_hud->selectBuyItem(3, "animals/zebra.ai");
        click(2000);
      }
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
            t->edgeBits = 0; // (its sides' cliff codes, byte 6, cleared too: flat ground has none)
            for (int c = 0; c < 4; c++)
              t->cornerHeight[c] = static_cast<uint8_t>(flat);
          }
        an.markDirty();
        int suit = an.suitability(ids[0], map, fn);
        check(("bare grass exhibit, two zebras: suitability " + std::to_string(suit)).c_str(), !clear || suit == 48);
        // (the original's: Full exhibit advice off)
        const bool fullAdvice = Features::suitabilityAdvice;
        Features::suitabilityAdvice = false;
        std::vector<std::pair<std::string, bool>> advice = an.advice(ids[0], map, fn);
        Features::suitabilityAdvice = true;
        const std::vector<std::pair<std::string, bool>> fuller = an.advice(ids[0], map, fn);
        Features::suitabilityAdvice = fullAdvice;
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
        // (Full exhibit advice: at 48 of the 50 a zebra wants, the same with
        // the native objects it lacks first)
        want.insert(want.begin(), {"There are not enough native objects in the exhibit to make Plains Zebra 1 happy.", true});
        check("and with Full exhibit advice, the missing native objects first", !clear || fuller == want);
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
        // (what its keeper puts down: zebra.ai cKeeperFoodType 0, herbchow -
        // "grass" isn't a food type, and a save holding it is refused)
        int food = world->getItems().addFood(penId, fx + 0.5f, fy + 0.5f, 2, "herbchow");
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
        // (zoo.exe animalInfoRefresh 0x4673b5: an egg shows full happiness,
        // food and suitability bars)
        {
          Animals::Member *shown = an.member(id1);
          const bool wasEgg = shown->egg;
          shown->egg = true;
          g_hud->refreshAnimal();
          const bool full = g_hud->animalBarValue(3104) == 100 && g_hud->animalBarValue(3108) == 100 &&
                            g_hud->animalBarValue(3110) == 100;
          shown->egg = wasEgg;
          g_hud->refreshAnimal();
          check("an egg's Animal Information shows full happiness, food and suitability (0x4673b5)", full);
          // (the exhibit bar's colour by the animal's own limits: red under
          // cPctHabitat x cHabitatPreference / 100, yellow under
          // cHabitatPreference, else green - 0x4673b5, 0x46790e, 0x418b8c)
          Animals::Type &kind = an.mutableTypes()[shown->type];
          const int keepPref = kind.habitatPreference, keepPct = kind.pctHabitat;
          const int v = g_hud->animalBarValue(3110);
          int bands[3];
          kind.habitatPreference = 101; kind.pctHabitat = 100; g_hud->refreshAnimal(); bands[0] = g_hud->animalBarBand(3110);
          kind.habitatPreference = v + 1; kind.pctHabitat = 1; g_hud->refreshAnimal(); bands[1] = g_hud->animalBarBand(3110);
          kind.habitatPreference = 0; kind.pctHabitat = 0; g_hud->refreshAnimal(); bands[2] = g_hud->animalBarBand(3110);
          kind.habitatPreference = keepPref; kind.pctHabitat = keepPct;
          g_hud->refreshAnimal();
          check(("the exhibit bar is red, yellow or green by the animal's own limits (0x4673b5: " + std::to_string(v) + " " +
                 std::to_string(bands[0]) + std::to_string(bands[1]) + std::to_string(bands[2]) + ")").c_str(),
                v >= 0 && bands[0] == 0 && bands[1] == 1 && bands[2] == 2);
        }
        click(3164);
        capture("animal_panel_general");
        click(3162);
        // Zookeeper Recommendations
        click(3122);
        capture("animal_advice");
        check("Zookeeper Recommendations open", g_hud->isPanelOpen(28));
        // (a toggle: clicked again, infoanm_KeeperPlaque_off 0x48330d shuts
        // them; a third click opens them again)
        click(3122);
        const bool shutAgain = !g_hud->isPanelOpen(28) && g_hud->isPanelOpen(6);
        click(3122);
        check("Zookeeper Recommendations clicked again close (0x48330d), and open again",
              shutAgain && g_hud->isPanelOpen(28));
        // A bottom list opened (smartstack off, as the original): Zoo Status
        // shuts (closeOtherPanels 0x443af8); the Staff List shuts the
        // Recommendations too (0x44410c), the Animal List doesn't
        {
          const bool keep = Features::smartStack;
          Features::smartStack = false;
          g_hud->setPanelOpen(14, true);
          g_hud->setPanelOpen(16, true);
          const bool animalList = g_hud->isPanelOpen(28) && !g_hud->isPanelOpen(14) && g_hud->isPanelOpen(6);
          g_hud->setPanelOpen(16, false);
          g_hud->setPanelOpen(14, true);
          g_hud->setPanelOpen(31, true);
          const bool staffList = !g_hud->isPanelOpen(28) && !g_hud->isPanelOpen(14) && g_hud->isPanelOpen(6);
          g_hud->setPanelOpen(31, false);
          Features::smartStack = keep;
          check(("a bottom list shuts Zoo Status; the Staff List the Recommendations too, the Animal List not (0x469fc4, 0x469627; " +
                 std::to_string(animalList) + std::to_string(staffList) + ")").c_str(),
                animalList && staffList);
          click(3122);
        }
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
        // Sold: its price by its happiness and health (zoo.exe
        // ZTAnimal::sellValue 0x48d21d, written out: $800 x 0.8 x
        // ((happiness + 100) x 0.005 + (health - cMinHits) / cMaxHits) x 0.5)
        double refund = an.sellValue(id2);
        const Animals::Member &sold = *an.member(id2);
        const Animals::Type &soldType = an.types()[sold.type];
        double expect = soldType.cost * 0.8 *
                        ((std::lround(sold.happiness) + 100) * 0.005 +
                         static_cast<double>(std::lround(sold.health) - soldType.minHits) / soldType.maxHits) *
                        0.5 * (sold.sick ? 0.5 : 1.0);
        cash = g_sim.cash();
        const double recycling = g_sim.history().back().lines[ZooSim::Recycling];
        const double purchases = g_sim.history().back().lines[ZooSim::AnimalPurchase];
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
              !an.member(id2) && std::abs(refund - expect) < 0.01 && std::abs(g_sim.cash() - cash - refund) < 0.01);
        // (booked on Recycling Benefit, as zoo.exe removeObject 0x50a0b0's
        // bookRefund 0x48d2da: Finances' 3939 sums books 9 and 10)
        check("the sale on Recycling Benefit, not Animal Purchases",
              std::abs(g_sim.history().back().lines[ZooSim::Recycling] - recycling - refund) < 0.01 &&
                  g_sim.history().back().lines[ZooSim::AnimalPurchase] == purchases);
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
          bool sounding = Audio::playing(-1);
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
        handleToolResult(world->mouseDown(px, py - 20), rm); handleToolResult(world->mouseUp(px, py - 20), rm);
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
      handleToolResult(world->mouseDown(px, py), rm); handleToolResult(world->mouseUp(px, py), rm);
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
        handleToolResult(world->mouseDown(px, py), rm); handleToolResult(world->mouseUp(px, py), rm);
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
          // (left on, closing Build Exhibit turns it off: zoo.exe
          // buldhab_Panel_hide 0x4e10d3 -> 0x4e1064(0))
          click(3240);
          g_hud->showPanel(0, "");
          g_hud->showPanel(8, "fence");
          g_hud->handleInputs(none);
          UiButton *gbb = dynamic_cast<UiButton *>(gb);
          check("closing Build Exhibit ends gate mode (0x4e10d3)", !g_hud->gateMode() && gbb && !gbb->isToggledOn());
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
    // (a keeper not visiting the exhibit, standing in its gate: the gate
    // stays shut - it opened for anyone within a tile of it, so for keepers
    // and tour guides walking past on the path)
    {
      Fences::Edge gate{false, -1, -1};
      for (const auto &kv : fn.pieces())
        if (kv.second.gate && fn.exhibitOf(kv.first) == pen)
          gate = kv.first;
      float passOpen = -1;
      for (Staff::Member &k : const_cast<std::vector<Staff::Member> &>(st.members()))
        if (gate.x >= 0 && passOpen < 0 && st.types()[k.type].kind == Staff::Kind::Keeper) {
          k.job = Staff::Job::None;
          const_cast<Fences::Piece *>(fn.at(gate))->open = 0;
          k.x = gate.alongX ? gate.x + 0.5f : static_cast<float>(gate.x);
          k.y = gate.alongX ? static_cast<float>(gate.y) : gate.y + 0.5f;
          k.path.clear();
          k.pathAt = 0;
          k.visit = -1;
          k.workCheck = 1e6f;
          passOpen = 0;
          for (int i = 0; i < 5; i++) {
            world->update(SDL_GetKeyboardState(nullptr), 0.1f);
            passOpen = std::max(passOpen, fn.at(gate)->open);
          }
        }
      check(("a gate stays shut for a keeper walking past it, not visiting (frame " + std::to_string(static_cast<int>(passOpen)) + ")").c_str(),
            passOpen == 0.0f);
    }
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
      // (guests go in by a building's door, which has to be on a path - a
      // building just touching one isn't used: placed, turned each way, and
      // kept the way its door lands on one)
      auto doorOnPath = [&](const PlacedObjects::Object &o) {
        for (auto [dx, dy] : world->getGuests().doorTiles(o))
          if (map.isPath(dx, dy))
            return true;
        return false;
      };
      auto addFacingPath = [&](const std::string &file, float x, float y) {
        for (int facing : {4, 0, 2, 6}) {
          if (world->objectFit(file, x, y, facing) != Fences::Fit::Ok ||
              !world->objectsMutable().add(file, x, y, facing))
            continue;
          if (doorOnPath(world->getObjects().objects().back())) {
            world->reindex();
            return true;
          }
          world->objectsMutable().remove(static_cast<int>(world->getObjects().objects().size()) - 1);
        }
        return false;
      };
      std::string stand = "scenery/building/hdogstnd.ai";
      int sx = -1, sy = -1;
      world->setObjectTool(stand, 175, 0);
      for (int r = 1; r < 25 && sx < 0; r++)
        for (int dy = -r; dy <= r && sx < 0; dy++)
          for (int dx = -r; dx <= r && sx < 0; dx++) {
            int tx = 11 + dx, ty = 42 + dy;
            // its door on a path (a tile's middle, or a corner)
            for (float off : {0.5f, 1.0f})
              if (sx < 0 && addFacingPath(stand, tx + off, ty + off)) {
                sx = tx;
                sy = ty;
              }
          }
      world->setObjectTool("", 0, 0);
      check("a spot for a hot dog stand by the path, its door on it", sx >= 0);
      // (not over the zoo's entrance: its footprint, 6 x 2 half tiles, read
      // from scenery/other/fgate.ai though it's saved as building/fgate - as
      // one tile, a stand went on the gate and the original dropped it)
      for (const PlacedObjects::Object &g : world->getObjects().objects())
        if (g.typeName == "fgate" && sx >= 0) {
          const PlacedObjects::Object &st = world->getObjects().objects().back();
          int gx = 0, gy = 0;
          if (IniReader *ai = rm->getIniReader(PlacedObjects::typeFile(rm, PlacedObjects::fileOf(g)))) {
            gx = ai->getInt("characteristics/integers", "cfootprintx", 0);
            gy = ai->getInt("characteristics/integers", "cfootprinty", 0);
            delete ai;
          }
          if ((g.facing & 6) == 2 || (g.facing & 6) == 6)
            std::swap(gx, gy);
          const bool over = std::fabs(st.x - g.x) < gx / 4.0f + 0.5f - 0.01f && std::fabs(st.y - g.y) < gy / 4.0f + 0.5f - 0.01f;
          char at[96];
          std::snprintf(at, sizeof at, " half tiles at %.1f,%.1f; the stand at %.1f,%.1f, in the zoo %d)", g.x, g.y, st.x, st.y,
                        static_cast<int>(world->getFences().insideZoo(static_cast<int>(std::floor(st.x)),
                                                                     static_cast<int>(std::floor(st.y)))));
          check(("the stand clear of the zoo gate (gate " + std::to_string(gx) + " x " + std::to_string(gy) + at).c_str(),
                gx * gy == 12 && !over);
        }
      if (sx >= 0) {
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
          // (its money as the original writes it, zoo.exe 0x488446: cut to
          // whole dollars, thousands grouped - $1,234.90 shows "$1,234")
          {
            PlacedObjects::Object &w = const_cast<PlacedObjects::Object &>(o);
            const double was = w.income;
            w.income = 1234.9;
            std::vector<Input> none;
            g_hud->handleInputs(none);
            capture("building_panel_money");
            UiText *in = dynamic_cast<UiText *>(g_hud->getElementById(4531));
            const std::string shown = in ? in->getText() : "";
            w.income = was;
            check(("Building Information's income cut and grouped (" + shown + ")").c_str(), shown == "$1,234");
          }
          click(4557);
          check("its price up a step ($2.25)", std::fabs(world->getObjects().objects().back().price - 2.25f) < 0.001f);
          click(4556);
          // A price typed in (zoo.exe infocom_ItemcostCost_click 0x6176c1):
          // "$" and "," taken out; up by raisePrice, down by lowerPrice, so
          // kept to the arrows' bounds
          if (UiText *typed = dynamic_cast<UiText *>(g_hud->getElementById(4555)); typed && typed->onCommit) {
            const PlacedObjects::Object &bp = world->getObjects().objects().back();
            const float lo = g_hud->shownLowCost, hi = g_hud->shownHighCost,
                        most = Original::economy().buildingUseCostMax;
            typed->onCommit("$3.50");
            const float a = bp.price;
            typed->onCommit("$1,000.00");
            const float b = bp.price;
            typed->onCommit("-5");
            const float c = bp.price;
            typed->onCommit("$2.00");
            check(("a typed price kept to the arrows' bounds: $3.50 -> " + std::to_string(a) + ", $1,000 -> " +
                   std::to_string(b) + ", -5 -> " + std::to_string(c) + " (0x6176c1)").c_str(),
                  std::fabs(a - std::min({3.5f, most, hi})) < 0.001f && std::fabs(b - std::min({1000.0f, most, hi})) < 0.001f &&
                      std::fabs(c - std::max({-5.0f, 0.0f, lo})) < 0.001f && std::fabs(bp.price - std::max(2.0f, lo)) < 0.5f);
          } else {
            check("a typed price (the price text 4555)", false);
          }
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
          // Shown again: the tab last picked (infogst_Panel_show 0x46a91b);
          // a name typed: taken with a letter in it, blanks not (0x618a35,
          // 0x449215)
          g_hud->showGuest(g.id);
          const UiButton *thoughtsTab = dynamic_cast<UiButton *>(g_hud->getElementById(3465));
          const bool keptTab = thoughtsTab && thoughtsTab->isToggledOn();
          const std::string nameWas = g.name;
          std::string blank = "?", named = "?";
          if (UiText *typed = dynamic_cast<UiText *>(g_hud->getElementById(3402)); typed && typed->onCommit) {
            typed->onCommit("   ");
            blank = gs.guests().front().name;
            typed->onCommit("Zoo Fan");
            named = gs.guests().front().name;
            typed->onCommit(nameWas);
          }
          check(("Guest Information: its last tab kept, renamed (blank: " + blank + ", typed: " + named + ") (0x46a91b, 0x618a35)").c_str(),
                keptTab && blank == nameWas && named == "Zoo Fan" && gs.guests().front().name == nameWas);
          click(3462);
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
              // (a donation is by the animals' average happiness, hab+0xe8:
              // none from an exhibit of unhappy animals - zoo.exe 0x42ea14)
              const int mood = world->getAnimals().exhibitHappiness(pen);
              char line[200];
              snprintf(line, sizeof line,
                       "exhibit books: viewed %.0f s, donations $%.2f, popularity %d, fence %d, animals' happiness %d",
                       ex->viewed, ex->donationsTotal, ex->popularity(gs.clock), world->getFences().condition(pen), mood);
              check(line, world->getFences().condition(pen) == 0 &&
                              (ex->viewed <= 0 || ex->donationsTotal > 0 || mood <= 0));
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
                  if (lx < 0 && byPath && addFacingPath(loo, tx + off, ty + off)) {
                    lx = tx + off;
                    ly = ty + off;
                  }
              }
          check("a spot for a restroom by the path, its door on it", lx >= 0);
          if (lx >= 0) {
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
                    if (!byPath || !addFacingPath(file, x, y))
                      continue;
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
                  // (and its door: a big building's can stand past the ring)
                  // (with the tiles back to the ring, so it's reached)
                  const size_t ringSize = ring.size();
                  for (auto door : world->getGuests().doorTiles(world->getObjects().objects().back())) {
                    auto [px, py] = door;
                    for (int step = 0; step < 6; step++) {
                      if (std::find(ring.begin(), ring.begin() + ringSize, std::make_pair(px, py)) != ring.begin() + ringSize)
                        break;
                      ring.push_back({px, py});
                      float ddx = x - (px + 0.5f), ddy = y - (py + 0.5f);
                      if (std::fabs(ddx) > std::fabs(ddy))
                        px += ddx > 0 ? 1 : -1;
                      else
                        py += ddy > 0 ? 1 : -1;
                    }
                  }
                  for (auto [px, py] : ring)
                    if (!map.isPath(px, py)) {
                      map.setPath(px, py, pathType);
                      laid.push_back({px, py});
                    }
                  world->reindex();
                  // (some guests set down at its door: on hilly ground the
                  // ring's sides can be different heights, not walked round)
                  // (a step off the door, on the path beside it: one standing
                  // on the door itself has no walk to it, and never goes in)
                  const std::vector<std::pair<int, int>> doors = world->getGuests().doorTiles(world->getObjects().objects().back());
                  std::vector<std::pair<int, int>> at;
                  for (auto [dx, dy] : doors)
                    for (auto [rx, ry] : ring)
                      if (std::abs(rx - dx) + std::abs(ry - dy) == 1 && map.isPath(rx, ry) &&
                          std::find(doors.begin(), doors.end(), std::make_pair(rx, ry)) == doors.end())
                        at.push_back({rx, ry});
                  if (at.empty())
                    at = doors;
                  if (at.empty())
                    at = ring;
                  int k = 0;
                  for (const Guests::Guest &g : gs.guests()) {
                    if (g.x < -999 || k >= 4)
                      continue;
                    Guests::Guest &gg = const_cast<Guests::Guest &>(g);
                    gg.x = at[(k * 3) % at.size()].first + 0.5f;
                    gg.y = at[(k * 3) % at.size()].second + 0.5f;
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
          // (by one of the guests on a path - one where no one else stands)
          float hx = -1, hy = -1;
          int guide = -1;
          for (auto it = gs.guests().rbegin(); it != gs.guests().rend(); ++it)
            if (const Guests::Guest &g = *it; guide < 0 && guideType >= 0 && g.x > -999 &&
                world->getMap().isPath(static_cast<int>(g.x), static_cast<int>(g.y))) {
              guide = st.hire(guideType, g.x, g.y, world->getMap(), world->getFences());
              hx = g.x;
              hy = g.y;
            }
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
            Research::Tick rt = rs.advance(3.0f, [](int, double) { return true; });
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
        // The Commerce list's headers (zoo.exe buttons104f_click 0x4f09ad): a click
        // on the one already chosen turns its column round; every number
        // column starts biggest first
        {
          g_hud->showPanel(14, "");
          g_hud->showTab(14, 4180);
          capture("zoo_commerce_sort");
          auto row = [](int id, int months, int visitors, double profit, double average) {
            return UiGameScreen::CommerceRow{id, "n", "t", months, visitors, profit, average};
          };
          const auto lo = row(1, 1, 1, 1.0, 1.0), hi = row(2, 9, 9, 9.0, 9.0);
          bool order = true;
          for (int col = 1; col < 5; col++)
            order = order && UiGameScreen::commerceFirst(hi, lo, col, false) && !UiGameScreen::commerceFirst(lo, hi, col, false) &&
                    UiGameScreen::commerceFirst(lo, hi, col, true) && !UiGameScreen::commerceFirst(hi, lo, col, true);
          check("the Commerce list's number columns: biggest first, smallest first when turned round", order);
          click(4177);
          const bool first = !g_hud->commerceSortUp(2);
          click(4177);
          const bool second = g_hud->commerceSortUp(2);
          click(4177);
          const bool third = !g_hud->commerceSortUp(2);
          click(4179);
          const bool other = !g_hud->commerceSortUp(4) && !g_hud->commerceSortUp(2);
          check("the Commerce list's headers: the first click chooses, the next turn it round and back",
                first && second && third && other);
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
          // Delete on it does nothing (zoo.exe mulanim_list_pickB 0x618451,
          // on the event table nothing fires; measured, #270)
          if (al && al->getItemCount() > 0) {
            SDL_Rect r = al->getScrollBounds();
            Input in{};
            in.type = InputType::POSITIONED;
            in.event = InputEvent::LEFT_CLICK;
            in.position = {r.x + r.w / 2, r.y + 2 + al->getRowHeight() / 2};
            in.x = in.position.x;
            in.y = in.position.y;
            std::vector<Input> inputs = {in};
            g_hud->handleInputs(inputs);
            g_hud->handleInputs(none);
            const int picked = world->getAnimals().selected;
            const size_t animalsBefore = world->getAnimals().members().size();
            Input del{};
            del.type = InputType::BUTTON;
            del.event = InputEvent::KEY_DOWN;
            del.key = SDLK_DELETE;
            std::vector<Input> keys = {del};
            g_hud->handleInputs(keys);
            const bool quiet = !g_hud->dialogOpen();
            if (g_hud->dialogOpen())
              g_hud->answerDialog(false);
            check(("Delete on the Animal List does nothing (0x618451 never fired, measured; picked " + std::to_string(picked) + ")").c_str(),
                  picked >= 0 && quiet && world->getAnimals().member(picked) &&
                      world->getAnimals().members().size() == animalsBefore);
          }
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
            UiTransform ut = hudTransform(renderer);
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
          t->edgeBits = 0; // (its sides' cliff codes, byte 6, cleared too: flat ground has none)
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
      check("a cliff: the brush up a unit, the ground beside it left where it was",
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
            // ours: a check. ZT_BEAR_SAVE=<file.zoo>: the zoo saved now - the
            // bear out, a man beside it - for the original to play on
            if (const char *save = std::getenv("ZT_BEAR_SAVE"); save && g_hud) {
              world->centreOn(b->x, b->y);
              SDL_Log("[Bear] saved %s at %.1f,%.1f: %d", save, b->x, b->y,
                      static_cast<int>(SaveGame::save(save, *world, g_sim, *g_hud, g_currentMapPath)));
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
            b->mauling = -1;
            b->hidden = false;
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
              // (standing there through the chase: no pick of its own - one
              // that rolls to go home, off the paths, has no way to the gate
              // and is gone, and the bear has nobody to chase)
              gg.animTime = 0;
              gg.playFor = 60.0f;
              gg.leaveCheck = 1000;
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
              spared = spared && b && b->prey < 0 && b->mauling < 0;
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
        std::snprintf(b, sizeof b, " a%d@%.0f,%.0f%s", m.type, m.x, m.y, m.female ? "f" : "m");
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
      world = g_world;
      // (compared straight after loading: a few ticks later an animal may
      // have walked across a tile line)
      std::string now = snapshot();
      for (int i = 0; i < 3; i++)
        world->update(SDL_GetKeyboardState(nullptr), 0.05f);
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
  // Exit Game, Main Menu and Load Game ask whether to save first (zoo.exe
  // 0x501699 string 151, 0x501d71 153, 0x4cb8ed 152; showDialog 0x4fff2c
  // style 3): Yes, No and Cancel at their x / y with the question picture;
  // Cancel stays, No goes on, Yes saves and then goes on; with Save Game
  // switched off it isn't asked. Then Save Game closes Game Options once it
  // saved (0x477004)
  {
    struct Ask {
      UiAction action;
      int text;
      const char *name;
    };
    const Ask asks[] = {{UiAction::GAME_EXIT, 151, "exit game"},
                        {UiAction::GAME_MAIN_MENU, 153, "main menu"},
                        {UiAction::GAME_LOAD, 152, "load game"}};
    const std::string file = outDir + "/save_first.zt1save";
    for (const Ask &k : asks) {
      const std::vector<int> on = {static_cast<int>(k.action)};
      g_queuedActions.clear();
      const bool stops = askSaveFirst(rm, k.action);
      const bool asked = g_hud->hasDialog() && g_hud->dialogMessage() == rm->getString(k.text) && !rm->getString(k.text).empty();
      auto shown = [](int id) {
        UiElement *e = g_hud->dialogElement(id);
        return e && !e->isHidden();
      };
      bool buttons = shown(45010) && shown(45011) && shown(45013) && !shown(45012);
      if (k.action == UiAction::GAME_EXIT) {
        // (drawn: Yes, No, Cancel left to right, evenly - their x 20, 144, 268)
        capture("save_first_question");
        auto x = [](int id) { return g_hud->dialogElement(id) ? g_hud->dialogElement(id)->getLastRect().x : 0; };
        const int gap = x(45011) - x(45010);
        buttons = buttons && gap > 0 && std::abs(x(45013) - x(45011) - gap) <= 1;
      }
      g_hud->cancelDialog();
      const bool stayed = !g_hud->hasDialog() && g_queuedActions.empty();
      askSaveFirst(rm, k.action);
      g_hud->answerDialog(false);
      const bool no = !g_hud->hasDialog() && g_queuedActions == on;
      g_queuedActions.clear();
      std::filesystem::remove(file);
      g_saveFirstFile = file;
      askSaveFirst(rm, k.action);
      g_hud->answerDialog(true);
      g_saveFirstFile.clear();
      const bool yes = std::filesystem::exists(file) && g_queuedActions == on;
      g_queuedActions.clear();
      std::filesystem::remove(file);
      check((std::string(k.name) + " asks to save first (" + std::to_string(k.text) + "): asked " + std::to_string(stops && asked) +
             ", Yes/No/Cancel " + std::to_string(buttons) + ", Cancel stays " + std::to_string(stayed) + ", No goes on " +
             std::to_string(no) + ", Yes saves then goes on " + std::to_string(yes))
                .c_str(),
            stops && asked && buttons && stayed && no && yes);
    }
    UiElement *save = g_hud->getElementById(1502);
    if (save)
      save->setDisabled(true);
    const bool goesOn = save && !askSaveFirst(rm, UiAction::GAME_EXIT) && !g_hud->hasDialog();
    if (save)
      save->setDisabled(false);
    check("Save Game switched off: Exit Game isn't asked", goesOn);
    const bool menuWas = g_hud->isPanelOpen(5);
    saveGameClicked(file);
    const bool closed = menuWas && std::filesystem::exists(file) && !g_hud->isPanelOpen(5);
    std::filesystem::remove(file);
    check("Save Game saved: Game Options closes", closed);
    if (!g_hud->isPanelOpen(5))
      g_hud->toggleGameMenu();
  }

  // (last: a few hundred seconds go by, which would move everyone about
  // for the tests after it)
  // Marine Mania in the tank: what can go in (zoo.exe 0x4df688), a
  // real shark, fed its chum and eating it
  {
    Fences &fn = world->getFences();
    WorldMap &map = world->getMap();
    const Fences::Exhibit *te = nullptr;
    for (const Fences::Exhibit &e : fn.exhibits())
      if (e.name == "Test Tank")
        te = &e;
    if (te) {
      for (int i = 0; i < 30; i++)
        fn.update(1.0f); // (the water in)
      // A land rock: refused; kelp and an undersea rock: in
      std::pair<int, int> spot{-1, -1};
      for (auto [x, y] : te->tiles)
        if (!fn.byTankLadder(*te, x, y) && spot.first < 0)
          spot = {x, y};
      Fences::Fit rock = world->objectFit("scenery/other/lrock1.ai", spot.first + 0.5f, spot.second + 0.5f, 4);
      int rockWhy = world->fitMessage;
      Fences::Fit kelp = world->objectFit("scenery/foliage/kelp.ai", spot.first + 0.5f, spot.second + 0.5f, 4);
      check(("in a tank: no land rock (" + std::to_string(rockWhy) + "), kelp yes").c_str(),
            rock != Fences::Fit::Ok && rockWhy == 10559 && kelp == Fences::Fit::Ok);
      Animals &an = world->getAnimals();
      int zebra = an.adopt(an.typeOfFile("animals/zebra.ai"), false, spot.first + 0.5f, spot.second + 0.5f, map, fn);
      int zebraWhy = an.placeMessage;
      // (the hammerhead at the 2 x 2 tank's middle: its footprint, 4 half
      // tiles, put down on a tile's middle would reach over the tank's wall,
      // which 0x4f7fbd refuses - #279)
      int minX = 1 << 30, minY = 1 << 30, maxX = -1, maxY = -1;
      for (auto [x, y] : te->tiles)
        minX = std::min(minX, x), minY = std::min(minY, y), maxX = std::max(maxX, x), maxY = std::max(maxY, y);
      int shark = an.adopt(an.typeOfFile("animals/hhshark.ai"), true, (minX + maxX + 1) * 0.5f, (minY + maxY + 1) * 0.5f,
                           map, fn);
      check(("a zebra not in a tank (" + std::to_string(zebraWhy) + "), a hammerhead in it").c_str(),
            zebra < 0 && zebraWhy == 10553 && shark >= 0);
      if (Animals::Member *sm = an.member(shark))
        sm->hunger = 80;
      world->getItems().removeIn(te->id, ZooItems::Kind::Food);
      float ate = 0;
      bool chum = false;
      for (int i = 0; i < 3000 && an.member(shark); i++) {
        world->update(SDL_GetKeyboardState(nullptr), 0.1f);
        for (const ZooItems::Item &it : world->getItems().items())
          chum = chum || (it.kind == ZooItems::Kind::Food && it.exhibit == te->id && it.food == "chum");
        ate = 80 - an.member(shark)->hunger;
        if (chum && ate >= 20)
          break;
      }
      world->centreOn(spot.first + 0.5f, spot.second + 0.5f);
      capture("shark_fed");
      check(("the hammerhead fed chum by its specialist and eats it (" + std::to_string(static_cast<int>(ate)) + ")").c_str(),
            chum && ate >= 20);
      if (shark >= 0)
        an.remove(shark);
      world->getAnimals().takeMessages();
    }
    // A tank's filter run down: a maintenance worker goes and services it
    // ("Going to service tank filter"), as good as new
    if (te && fn.filters().empty())
      for (int fy = -4; fy <= 4 && fn.filters().empty(); fy++)
        for (int fx = -4; fx <= 4 && fn.filters().empty(); fx++) {
          int tank = -1;
          int x = te->tiles.begin()->first + fx, y = te->tiles.begin()->second + fy;
          if (fn.filterFit(x, y, map, &tank) == Fences::Fit::Ok && tank == te->id)
            fn.placeFilter(x, y, map);
        }
    world->reindex();
    if (!fn.filters().empty()) {
      Staff &st = world->getStaff();
      Fences::Filter &f = const_cast<Fences::Filter &>(fn.filters().front());
      f.health = 2;
      int maint = -1;
      for (const Staff::Member &m : st.members())
        if (st.types()[m.type].kind == Staff::Kind::Maint)
          maint = m.id;
      if (maint < 0) {
        int fx = f.x, fy = f.y;
        for (int r = 2; r < 10 && maint < 0; r++)
          for (int dx = -r; dx <= r && maint < 0; dx++)
            maint = st.hire(st.typeOfFile("staff/maint.ai"), fx + dx + 0.5f, fy - r + 0.5f, map, fn);
      }
      bool going = false;
      for (int i = 0; i < 3000 && f.health < fn.filterType().startHealth - 0.01f; i++) {
        world->update(SDL_GetKeyboardState(nullptr), 0.1f);
        for (const Staff::Member &m : st.members())
          going = going || m.dutyId == 10515;
      }
      check(("a run-down filter serviced by a maintenance worker (health " + std::to_string(static_cast<int>(f.health)) + ")").c_str(),
            going && f.health >= fn.filterType().startHealth - 0.01f);
    }
  }

  // The main bar's guest meter (zoo.exe 0x435055 -> 0x41d15d): the
  // rating's average of the guests in the zoo, in whole numbers, (it + 100)
  // x 100 / 200 - an average of -1 shows 49 - and 0 with none (last: its
  // guests and time would move the checks after it)
  if (g_world && g_hud) {
    std::vector<Input> none;
    Guests &gs = g_world->getGuests();
    const auto *meter = dynamic_cast<UiStatusImage *>(g_hud->getElementById(1013));
    gs.clear();
    SDL_Delay(4100);
    worldInputs(renderer, rm, none);
    const int empty = meter ? meter->getValue() : -1;
    const bool noEntrance = !gs.hasEntrance();
    if (noEntrance)
      gs.setEntrance(40, 40);
    for (int k = 0; k < 3; k++)
      gs.admit(0);
    SDL_Delay(4100);
    worldInputs(renderer, rm, none);
    const Guests::Census c = gs.census();
    const int shown = meter ? meter->getValue() : -1;
    SDL_Log("[Check] guest meter: empty %d, %d guests in the zoo averaging %d shows %d", empty, c.guests, c.happiness,
            shown);
    check("the guest meter: (the guests' whole-number average + 100) x 100 / 200, 0 with none (0x41d15d)",
          empty == 0 && Original::mainBarMeter(-1, 1) == 49 && Original::mainBarMeter(0, 2) == 50 &&
              Original::mainBarMeter(37, 0) == 0 && c.guests >= 1 && shown == Original::mainBarMeter(c.happiness, c.guests));
    if (noEntrance)
      gs.setEntrance(-1, -1);
  }

  if (timings)
    fclose(timings);
  return 0;
}

// ----------------------------------------------------------------------------
// The credits (zoo.exe 0x514b27 creditsLoadPages, 0x41a430 creditsTick,
// 0x6162c0 creditsShowPage): the pages' layout ids from every
// ui/credits/credit*.cfg with a version over 0 (credits.cfg, with none, is
// left out), in file-name order, and the delay; one page shown at a time,
// the next every delay; after the last the credits close
// ----------------------------------------------------------------------------
static std::vector<int> g_creditPages;
static Uint32 g_creditDelayMs = 8000, g_creditNextAt = 0;
static size_t g_creditPage = 0;

static void creditsShowPage(UiLayout *layout, size_t page) {
  for (size_t i = 0; i < g_creditPages.size(); i++)
    if (UiElement *e = layout->getElementById(g_creditPages[i]))
      e->setHidden(i != page);
}

static void creditsLoad(UiLayout *layout, ResourceManager *rm) {
  g_creditPages.clear();
  g_creditDelayMs = 8000;
  for (const std::string &cfg : rm->listResources("ui/credits/credit", ".cfg")) {
    IniReader *ini = rm->getIniReader(cfg);
    if (!ini)
      continue;
    if (ini->getInt("credits", "version", 0) > 0) {
      g_creditDelayMs = static_cast<Uint32>(ini->getInt("credits", "delay", 8)) * 1000;
      for (const std::string &section : ini->getSections())
        if (section != "credits")
          if (int id = ini->getInt(section, "layout", 0))
            g_creditPages.push_back(id);
    }
    delete ini;
  }
  g_creditPage = 0;
  g_creditNextAt = SDL_GetTicks() + g_creditDelayMs;
  creditsShowPage(layout, 0);
  SDL_Log("[Credits] %zu pages, %u ms each", g_creditPages.size(), g_creditDelayMs);
}

// True once the last page has had its time: the credits are over
static bool creditsTick(UiLayout *layout) {
  if (g_creditPages.empty() || SDL_GetTicks() < g_creditNextAt)
    return false;
  g_creditNextAt = SDL_GetTicks() + g_creditDelayMs;
  if (++g_creditPage >= g_creditPages.size())
    return true;
  creditsShowPage(layout, g_creditPage);
  return false;
}

int main(int argc, char *argv[]) {
  SDL_SetMainReady();
  // The ZT1 Lab app's engine (--lab-host) draws for the app and is never
  // heard: a silent audio device, so only the game in its Play view plays
  // and never shown: no waiting for the screen's refresh (reading each
  // frame back waited on it)
  bool labHost = false;
  for (int i = 1; i < argc; i++)
    if (std::string(argv[i]) == "--lab-host") {
      labHost = true;
      SDL_SetHint(SDL_HINT_AUDIO_DRIVER, "dummy");
      SDL_SetHint(SDL_HINT_RENDER_VSYNC, "0");
    }
#ifdef _WIN32
  // This SDL build's WASAPI backend plays the game's audio slowly and
  // scratchily on some machines (the same files play fine in any other
  // player); DirectSound doesn't. DirectSound by default; SDL_AUDIODRIVER
  // set in the environment still wins. (Not for the Lab's engine: the
  // variable would outrank its silent device)
  if (!labHost && SDL_getenv("SDL_AUDIODRIVER") == NULL)
    SDL_setenv_unsafe("SDL_AUDIODRIVER", "directsound", 0);
#endif

  // Initialize memory tracking and crash dump system
  // ZT_MEMORY_DUMP_INIT();
  // Auto-dump every 60 seconds (helps catch state before crash)
  // MemoryDumpLog::get().setAutoDumpInterval(60);
  SDL_Log("Memory tracking initialized");

  // Where the game is (asked for once, when the engine's installed on its
  // own) and where our settings, mods and progress are kept (Paths.hpp)
  if (!Paths::setup())
    return 1;
  Config config(Paths::configFile());
  SDL_Log("ZT1-Engine %s%s", Updater::version(),
          Updater::testBuild() ? " (test release: no updates)" : Updater::official() ? "" : " (a build of one's own)");
  for (int i = 1; i < argc; i++)
    if (std::string(argv[i]) == "--check-update")
      return Updater::checkNowCli(i + 1 < argc && std::string(argv[i + 1]) == "--install");
  RenderSettings::uiScaleSetting = config.getUiScale();
  g_widescreen = config.getWidescreen();
  g_timedUnlocks = config.getTimedUnlocks();
  UiGameScreen::minimumMessageInterval = config.getMinimumMessageInterval();
  Features::allUnlocked = !g_timedUnlocks;
  // The extras switched on or off in the Game Options (zoo.ini [Extras]),
  // and its volumes and scrolling at work
  Settings::loadExtras();
  // addition: startup checks respect the saved switch before the worker starts.
  bool updaterDiagnostic = false;
  for (int i = 1; i < argc; ++i)
    if (std::string(argv[i]) == "--update-ui-checks") updaterDiagnostic = true;
  if (!labHost && !updaterDiagnostic)
    Updater::start();
  // ours: resources own renderer textures and must be destroyed before
  // the window. Keep construction timing (including headless CLI modes),
  // but declare its owner first so reverse destruction order is correct.
  std::unique_ptr<Window> windowLifetime;
  ResourceManager resource_manager(&config);
  // (HD art for recoloured art finds the art in its own palette here)
  HdArt::setResources(&resource_manager);
  // A mod added from the command line (the Lab's import): --import-mod
  // <a .ztd, a .zip or a mod's folder> - what happened, as JSON, and done
  for (int i = 1; i + 1 < argc; i++)
    if (std::string(argv[i]) == "--import-mod") {
      ModImport::Result r = ModImport::import(argv[i + 1]);
      auto q = [](const std::string &s) {
        std::string o = "\"";
        for (char c : s)
          o += c == '"' || c == '\\' ? std::string("\\") + c : std::string(1, c);
        return o + "\"";
      };
      std::printf("{\"ok\": %s, \"folder\": %s, \"name\": %s, \"message\": %s}\n", r.ok ? "true" : "false",
                  q(r.folder).c_str(), q(r.name).c_str(), q(r.message).c_str());
      std::fflush(stdout);
      return r.ok ? 0 : 1;
    }
  // Sound effects, and every button's click (uiclick.wav)
  Sound::get().init(&resource_manager);
  UiButton::clickSound = [] { Sound::get().play("sounds/uiclick"); };
  g_console.run = [&resource_manager](const std::vector<std::string> &words) {
    return devCommand(words, &resource_manager);
  };

  // FSR runs on the GPU, which needs SDL's OpenGL renderer (shaders). If
  // that is not available the art is upscaled once when it loads instead.
  // ours: OpenGL when a setting that needs its shaders is on as the game
  // starts, otherwise Direct3D 11 - the same pictures, faster
  // (Display::startupRenderer). The Lab's engine stays on OpenGL.
  // (SDL_RENDER_DRIVER set in the environment still wins: the checks)
  SDL_SetHint(SDL_HINT_RENDER_DRIVER, labHost ? "opengl" : Display::startupRenderer());
  // ours: and its draws batched. Named by the hint, SDL turns batching off,
  // and every sprite, tile and shadow went to the driver as a draw of its
  // own (half of each frame spent there). Our own OpenGL passes (FSR, the
  // lighting) send SDL's batch first (SDL_FlushRenderer) as batching needs.
  // (SDL3's renderers always batch: SDL2's SDL_HINT_RENDER_BATCHING is gone)
  windowLifetime=std::make_unique<Window>("ZT1-Engine",config.getScreenWidth(),config.getScreenHeight(),60.0f);
  Window &window=*windowLifetime;
  // The Display, Graphics and Accessibility settings at work (frame rate,
  // vsync, colours, the art's look)
  Display::init(window.window, window.renderer);
  Display::noteRunning(window.renderer); // (what SDL made of the pick: a fallback shows)
#ifdef _WIN32
  D3D11Util::installTextureMaker(window.renderer); // (ours: new textures made with their pixels)
#endif
  const Display::Art artAtStart = Display::artAtStart();
  // The Game Options' volumes and scrolling at work (after the window: it
  // opens the audio)
  Settings::apply();
  // Inside the ZT1 Lab app: the game's window in its Play view (--embed
  // <window>), or the Lab's engine kept out of sight, drawing for the app
  // (--lab-host <port>)
  for (int i = 1; i + 1 < argc; i++)
    if (std::string(argv[i]) == "--embed" && !Embed::into(window.window, argv[i + 1]))
      SDL_Log("[Embed] couldn't go into window %s", argv[i + 1]);
  for (int i = 1; i < argc; i++)
    if (std::string(argv[i]) == "--lab-host")
      labHostHide(window.window);
  // The player's own window put back as they left it (full screen,
  // maximised, where it was) and kept from now on - an ordinary run only:
  // never in the Lab, nor a test run, which need the size they're given
  {
    bool ordinary = !Embed::active();
    for (int i = 1; i < argc; i++)
      for (const char *special :
           {"--lab-host", "--lab", "--panel-shots", "--render-maps", "--check-update", "--hd-export", "--hd-finish",
            "--hd-survey", "--hd-index", "--update-ui-checks"})
        if (std::string(argv[i]) == special)
          ordinary = false;
    if (ordinary)
      window.placeSaved();
  }
  // FSR on the graphics card whenever it can run (used while it's picked);
  // the art upscaled as it loads only for MMPX, or for FSR without it
  bool gpuFsr = GpuFsr::init(window.renderer, config.getArtSharpness());
  ArtScaler::Mode artMode = artAtStart == Display::Art::Mmpx                ? ArtScaler::Mode::Mmpx
                            : (artAtStart == Display::Art::Fsr || artAtStart == Display::Art::Stylized) && !gpuFsr
                                ? ArtScaler::Mode::Fsr
                                                                          : ArtScaler::Mode::Off;
  ArtScaler::configure(artMode, config.getArtUpscaleFactor(), config.getArtSharpness());
  // The original's cursors (res0.dll): the arrow, hot spot at its tip, and
  // the full bulldozer while it's picked - at the Cursor size picked
  // (Accessibility), made again when it changes
  auto loadCursors = [&resource_manager, &window](int size) {
    SDL_Cursor *old[] = {g_arrowCursor, g_bulldozeCursor, g_tankWallCursor};
    g_arrowCursor = resource_manager.getCursor(9, 1, 2, size);
    g_bulldozeCursor = resource_manager.getCursor(16, 2, 29, size);
    // (cursor slot 4: res0.dll group 204, its cursor 13 - hot spot 16, 23)
    g_tankWallCursor = resource_manager.getCursor(13, 16, 23, size);
    window.set_cursor(g_arrowCursor);
    for (SDL_Cursor *c : old)
      if (c)
        SDL_DestroyCursor(c);
  };
  loadCursors(Display::cursorSize());
  Display::onCursorSize = loadCursors;

  // addition: offline HD jobs must not repeatedly take desktop focus.
  for(int a=1;a<argc;++a)if(std::string(argv[a]).starts_with("--hd-")){SDL_HideWindow(window.window);break;}
  LoadScreen::run(&window, &config, &resource_manager);
  Mods::markApplied();
  for (int i = 1; i + 1 < argc; ++i)
    if (std::string(argv[i]) == "--update-ui-checks") {
      SDL_HideWindow(window.window);
      return UiUpdate::checks(window.renderer, &resource_manager, argv[i + 1]);
    }
  // The Lab's HD art mod (HdArt.hpp): the map art's frames written out
  // (--hd-export <folder> [prefix...]), or upscaled ones tidied into the mod
  // (--hd-finish <frames> <upscaled> <mod's hd folder>)
  for (int i = 1; i < argc; i++) {
    if (std::string(argv[i]) == "--hd-index" && i+1<argc) {extern int hdWriteIndex(ResourceManager *,const std::string &);return hdWriteIndex(&resource_manager,argv[i+1]);}
    if (std::string(argv[i]) == "--hd-export" && i + 1 < argc) {
      std::vector<std::string> prefixes(argv + i + 2, argv + argc);
      extern void hdSetExportRenderer(SDL_Renderer *);hdSetExportRenderer(window.renderer);
      return HdArt::exportFrames(&resource_manager, argv[i + 1], prefixes) >= 0 ? 0 : 1;
    }
    if (std::string(argv[i]) == "--hd-survey") {
      HdArt::survey(&resource_manager, std::vector<std::string>(argv + i + 1, argv + argc));
      return 0;
    }
    if (std::string(argv[i]) == "--hd-finish" && i + 3 < argc)
      return HdArt::finishFrames(argv[i + 1], argv[i + 2], argv[i + 3]) > 0 ? 0 : 1;
  }
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
  for (int n = 1; n < argc; n++) if (std::string(argv[n]) == "--lab" || std::string(argv[n]) == "--lab-host") g_labModContext = true;
  Mods::setDisableCheck([](const std::string &folder) {
    return g_world && (g_hud || g_labModContext) ? g_world->modRequiredReason(folder) : "";
  });
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

  // Which scenarios are done (Paths: the checkout's saves/ when portable,
  // %LocalAppData%\zt1-engine\saves when installed)
  if (!g_scenarioManager->getDatabase()->loadState(Paths::scenarioProgressFile())) {
    // No save found, create default
    SDL_Log("No save found, creating default save file.");
    g_scenarioManager->getDatabase()->saveState(Paths::scenarioProgressFile());
  }

  // Sync scenarios with database and sort by difficulty
  g_scenarioManager->syncWithDatabase();
  g_scenarioManager->sortByDifficulty();

  // The Options window on the main menu (the optionswindow extra)
  resource_manager.provide("ui/startup.lyt", mainMenuWithOptions);
  g_menuOptions = new UiOptions(&resource_manager);
  g_cashStrings = &resource_manager;
  IniReader *lyt_reader = resource_manager.getIniReader(
      getLayoutPath(&resource_manager, "startup.lyt"));
  UiLayout *layout = new UiLayout(lyt_reader, &resource_manager);
  hookMainMenu(layout, &resource_manager);
  g_currentState = LayoutState::MAIN_MENU;
  UiUpdate updateUi(&resource_manager);
  g_menuOptions->onUpdate = [&updateUi] { updateUi.open(); };

  // ZT1 Lab: the live view for what's found in zoo.exe (--lab [map]); for
  // the Lab app, the same driven over a local connection (--lab-host [port])
  for (int i = 1; i < argc; i++) {
    if (std::string(argv[i]) == "--lab")
      return runLab(window.window, window.renderer, &resource_manager, g_scenarioManager, g_world,
                    i + 1 < argc ? argv[i + 1] : "deathmtn");
    if (std::string(argv[i]) == "--lab-host")
      return runLab(window.window, window.renderer, &resource_manager, g_scenarioManager, g_world, "deathmtn",
                    i + 1 < argc ? std::atoi(argv[i + 1]) : 18490,
                    [&](const std::vector<std::string> &words) {
                      if (!words.empty() && words[0] == "camerarendercheck") {
                        extern std::string runCameraRenderChecks(SDL_Renderer *);
                        return runCameraRenderChecks(window.renderer);
                      }
                      return g_console.run(words);
                    });
  }

  for (int i = 1; i + 1 < argc; i++) {
    if (std::string(argv[i]) == "--panel-shots") {
      // ZT_AUDIO_CHECKS=1: the sound as SDL2_mixer made it, measured through
      // SDL's silent disk driver (src/AudioChecks.cpp)
      if (std::getenv("ZT_AUDIO_CHECKS")) {
        extern std::string runAudioChecks(ResourceManager *, const std::string &);
        std::filesystem::create_directories(argv[i + 1]);
        return runAudioChecks(&resource_manager, argv[i + 1]).find("FAIL ") == std::string::npos ? 0 : 1;
      }
      Animation::setGradualZoomedIn(false); // (the checks' pictures: every frame complete)
      GraphicsQuality::forThisRun(GraphicsQuality::High); // (the same pictures on every PC)
      return runPanelShots(window.renderer, &resource_manager,
                           g_scenarioManager, g_world, argv[i + 1],
                           i + 2 < argc ? argv[i + 2] : "deathmtn");
    }
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

  // ours: opt-in long benchmarks use this same Play loop and presentation.
  GameBenchmark gameBenchmark;
  // (ZT_GAME_BENCH_ZOO: a player's saved zoo timed instead - their graphics
  // settings, its own view; GameBenchmark::startZoo)
  if (const char *benchZoo = std::getenv("ZT_GAME_BENCH_ZOO")) {
    if (!std::getenv("ZT_GAME_BENCH_OUT") || !gameLoad(&resource_manager, benchZoo) ||
        !gameBenchmark.startZoo(*g_world, window.renderer)) return 1;
    g_currentState = LayoutState::GAME_LOOP;
  } else if (std::getenv("ZT_GAME_BENCH_OUT")) {
    GraphicsQuality::forThisRun(GraphicsQuality::High); // (or ZT_GRAPHICS_PRESET's)
    ItemCatalog::get().load(&resource_manager);Research::get().load(&resource_manager);
    if (!gameBenchmark.start(*g_world,&resource_manager,window.renderer)) return 1;
    createHud(&resource_manager,g_world,1000000);
    g_currentState=LayoutState::GAME_LOOP;
    g_gameSpeed=static_cast<float>(gameBenchmark.rate());
    syncGameSpeedControl();
  }
  SampleProfiler::startFromEnvironment(); // ours: off unless asked for
  InputManager input_manager;
  std::vector<Input> inputs;

  int running = 1;
  UiAction action = UiAction::NONE;

  // Delta time tracking
  Uint32 lastFrameTime = SDL_GetTicks();
  float deltaTime = 0.0f;

  LayoutState lastState = g_currentState;
  std::string modSelection = Mods::selectionKey();
  Uint32 modPoll = 0;
  while (running > 0) {
    const Uint32 modNow = SDL_GetTicks();
    if (modNow - modPoll >= 250) {
      modPoll = modNow;
      if (Mods::selectionKey() != modSelection) {
        applyModSelection(&resource_manager, layout, lyt_reader);
        modSelection = Mods::selectionKey();
        lastFrameTime = SDL_GetTicks();
        g_speedClock.clearPending();
        continue;
      }
    }
    // The menus have music, a game has none (the world's ambience instead)
    if (g_currentState != lastState) {
      g_speedClock.reset();g_gameSpeed=1;
      lastFrameTime=SDL_GetTicks();
      if (g_currentState == LayoutState::GAME_LOOP)
        resource_manager.fadeMenuMusic();
      else if (lastState == LayoutState::GAME_LOOP) {
        Audio::halt(-1);
        resource_manager.playMenuMusic();
      }
      lastState = g_currentState;
    }
    // In the Lab with its Play view not showing: the game holds still (no
    // updates, its sound paused) and carries on where it was when shown
    static bool heldHidden = false;
    if (Embed::hidden()) {
      if (!heldHidden) {
        Audio::pause(-1);
        Audio::pauseMusic();
        heldHidden = true;
      }
      if (Embed::quitAsked())
        break;
      SDL_PumpEvents();
      SDL_Delay(50);
      lastFrameTime = SDL_GetTicks();
      g_speedClock.clearPending();
      continue;
    }
    if (heldHidden) {
      Audio::resume(-1);
      Audio::resumeMusic();
      heldHidden = false;
      lastFrameTime = SDL_GetTicks();
      g_speedClock.clearPending();
    }
    // Calculate delta time
    Uint32 currentTime = SDL_GetTicks();
    deltaTime = (currentTime - lastFrameTime) / 1000.0f; // Convert to seconds
    lastFrameTime = currentTime;

    // Game Options' Install now started the installer: close for it
    if (Updater::installing())
      running = 0;
    if(gameBenchmark.enabled())gameBenchmark.beforeFrame(*g_world);
    FrameProfile::begin();
    window.clear();FrameProfile::phase("clear");
    inputs = input_manager.getInputs();FrameProfile::phase("input collection");
    static bool spriteCulling = true;
    if (Embed::active()) for (const Input &in : inputs)
      if (in.event == InputEvent::KEY_DOWN && in.key == SDLK_F8) {
        extern void worldSpriteCulling(bool);
        spriteCulling = !spriteCulling; worldSpriteCulling(spriteCulling);
        SDL_Log("[FrameProfile] sprite pruning %s (Lab reference comparison)", spriteCulling ? "on" : "off");
      }
    // F1 opens the Help window on the topic it was left on (zoo.exe gameKeys
    // 0x4cb340: openHelp(-1) 0x484a32)
    if (g_currentState == LayoutState::GAME_LOOP && g_hud && !UiText::isEditing())
      for (const Input &in : inputs)
        if (in.event == InputEvent::KEY_DOWN && in.key == SDLK_F1)
          g_hud->openHelp(-1);
    if(g_currentState==LayoutState::GAME_LOOP)
      for(const Input &in:inputs)if(in.event==InputEvent::KEY_DOWN && in.key==SDLK_F9) {
        FrameProfile::start();int width=0,height=0;SDL_GetCurrentRenderOutputSize(window.renderer,&width,&height);
        ZT_RendererInfo device{};ZT_GetRendererInfo(window.renderer,&device);
        SDL_Log("[FrameProfile] renderer=%s accelerated=%d targetTextures=%d vsync=%d",device.name,
          (device.flags&ZT_RENDERER_ACCELERATED)!=0,(device.flags&ZT_RENDERER_TARGETTEXTURE)!=0,(device.flags&ZT_RENDERER_PRESENTVSYNC)!=0);
        SDL_Log("[FrameProfile] context guests=%zu animals=%zu size=%dx%d zoom=%.2f paused=%d spriteCulling=%d",
          g_world->getGuests().guests().size(),g_world->getAnimals().members().size(),width,height,
          g_world->getCamera().zoom,g_world->isPaused(),spriteCulling);
      }
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
      if (input.event == InputEvent::KEY_DOWN && input.key == SDLK_Z && (SDL_GetModState() & SDL_KMOD_CTRL) &&
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
      if (input.event == InputEvent::KEY_DOWN && (input.key == SDLK_Q || input.key == SDLK_E) &&
          !UiText::isEditing() && !g_console.isOpen() && g_currentState == LayoutState::GAME_LOOP &&
          g_world) {
        if (Features::hotkeys && g_hud && !g_world->getObjectTool().empty())
          g_hud->rotateHeld(input.key == SDLK_E ? 1 : -1);
        else
          g_world->rotateView(input.key == SDLK_E ? 1 : -1);
      }
      // Tab (Shift+Tab back): how a path or walkway drag lays out
      if (input.event == InputEvent::KEY_DOWN && input.key == SDLK_TAB && !UiText::isEditing() &&
          g_currentState == LayoutState::GAME_LOOP && g_world && !g_world->getPathTool().empty() &&
          Features::pathModes)
        g_world->cyclePathMode((SDL_GetModState() & SDL_KMOD_SHIFT) ? -1 : 1);
      // Tab (Shift+Tab back) changes how a fence drag lays out
      if (input.event == InputEvent::KEY_DOWN && input.key == SDLK_TAB &&
          !UiText::isEditing() && g_currentState == LayoutState::GAME_LOOP && g_world &&
          g_world->getFenceTool() >= 0 && Features::fenceModes)
        g_world->cycleFenceMode((SDL_GetModState() & SDL_KMOD_SHIFT) ? -1 : 1);
    }

    std::vector<Input> updateInputs = inputs;
    mapInputsToLayout(updateInputs, getUiTransform(window.renderer));
    if (g_currentState == LayoutState::MAIN_MENU && layout &&
        !(g_menuOptions && g_menuOptions->isOpen()) && updateUi.handleInputs(updateInputs, layout)) {
      action = (UiAction)9999;
    } else if (layout && g_menuOptions && g_menuOptions->isOpen()) {
      // (the Options window over the main menu takes every input)
      std::vector<Input> layoutInputs = inputs;
      mapInputsToLayout(layoutInputs, optionsTransform(window.renderer));
      // (its Mod Menu button gives back the main menu's own action)
      action = g_menuOptions->handleInputs(layoutInputs);
      if (action == UiAction::NONE)
        action = (UiAction)9999;
    } else if (layout) {
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
    // The credits' pages, one at a time (zoo.exe 0x41a430 creditsTick, 0x6162c0
    // creditsShowPage): the next every delay; after the last, back to the menu
    if (g_currentState == LayoutState::CREDITS && layout && creditsTick(layout))
      action = UiAction::CREDITS_EXIT;
    // (the Mod Menu's repository list and its downloads kept up to date)
    if (g_currentState == LayoutState::MOD_MENU && layout)
      ModMenu::tick(layout, &resource_manager);

    // A menu screen's "close me" button (action=2 target=-1: the map select,
    // scenario and Mod Menu screens' Back): back to the main menu, as the
    // original (in a game, the HUD's panels handle their own)
    if (g_currentState != LayoutState::GAME_LOOP && isPanelClose(action))
      action = UiAction::SCENARIO_BACK_TO_MAIN_MENU;
    // A scenario's ending chose (scenarioComplete): the next tutorial, or a
    // menu screen, one step a frame
    bool queuedAction = false;
    if (!g_nextScenario.empty() && g_currentState == LayoutState::GAME_LOOP) {
      const std::string next = g_nextScenario;
      g_nextScenario.clear();
      for (int i = 0; const ScenarioInfo *info = g_scenarioManager ? g_scenarioManager->getScenario(i) : nullptr; i++)
        if (SDL_strcasecmp(info->scenarioPath.c_str(), next.c_str()) == 0) {
          destroyHud();
          startScenario(&resource_manager, *info);
          break;
        }
    } else if (!g_queuedActions.empty()) {
      action = static_cast<UiAction>(g_queuedActions.front());
      g_queuedActions.erase(g_queuedActions.begin());
      // (queued by a dialog's answer or a scenario's ending: Exit Game,
      // Main Menu and Load Game go on without asking to save)
      queuedAction = true;
    }

    switch (action) {
    // (zoo.exe startup_Exit_click 0x4f27a8 -> 0x501224: the main panel
    // hidden, main_Panel_hide 0x5b14a4, then the program quits)
    case UiAction::STARTUP_EXIT:
      running = false;
      break;

    case UiAction::STARTUP_CREDITS:
      delete layout;
      lyt_reader = resource_manager.getIniReader(
          getLayoutPath(&resource_manager, "credits.lyt"));
      layout = new UiLayout(lyt_reader, &resource_manager);
      g_currentState = LayoutState::CREDITS;
      // The pages (zoo.exe 0x514b27 creditsLoadPages): every
      // ui/credits/credit*.cfg, in file-name order, with a version over 0: its
      // delay (seconds) and each section's layout id; only the first page shows
      creditsLoad(layout, &resource_manager);
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

    // Get New Zoo Tycoon Items: the Mod Menu with its switch on (off, as
    // ours always was: nothing - the original's download service is gone)
    case UiAction::STARTUP_ZOO_ITEMS:
      if (!Features::modMenu)
        break;
      delete layout;
      // (on the Tutorial & Scenario screen's layout)
      layout = ModMenu::open(&resource_manager, getLayoutPath(&resource_manager, "scenario.lyt"));
      g_currentState = LayoutState::MOD_MENU;
      break;
    case UiAction::MOD_LIST_SELECTION:
      if (g_currentState == LayoutState::MOD_MENU)
        ModMenu::selectionChanged(layout, &resource_manager);
      break;
    case UiAction::MOD_REPOSITORY:
      if (g_currentState == LayoutState::MOD_MENU)
        ModMenu::repository(layout, &resource_manager);
      break;
    case UiAction::MOD_ADD:
      if (g_currentState == LayoutState::MOD_MENU)
        ModMenu::addMod(layout, &resource_manager);
      break;
    case UiAction::MOD_FOLDER:
      if (g_currentState == LayoutState::MOD_MENU)
        ModMenu::chooseFolder(layout, &resource_manager);
      break;
    case UiAction::MOD_SORT:
      if (g_currentState == LayoutState::MOD_MENU)
        ModMenu::sort(layout, &resource_manager);
      break;
    case UiAction::MOD_RESTART:
      // Switches apply automatically between frames, including embedded Play.
      break;

    case UiAction::STARTUP_PLAY_SCENARIO:
      delete layout;
      lyt_reader = resource_manager.getIniReader(
          getLayoutPath(&resource_manager, "scenario.lyt"));
      layout = new UiLayout(lyt_reader, &resource_manager);
      g_currentState = LayoutState::SCENARIO_SELECT;
      // (what's open and done, as zoo.ini [scenario] has it now)
      g_scenarioManager->syncWithDatabase();
      populateScenarioList(layout, g_scenarioManager);
      break;

    // The main menu's Load Saved Game (the dialog; zoo.exe startup_LoadGame_click
    // 0x4cc7d1 -> 0x4cc606: loadZooDialog 0x4cc0d7, and once a zoo is loaded the
    // start-up screen, uiFind 0x1a, hidden) and Continue Saved Game (the
    // latest save)
    case UiAction::STARTUP_LOAD_GAME:
    case UiAction::STARTUP_CONTINUE_GAME: {
      std::string path;
      // (Continue: zoo.exe startup_ContinueGame_click 0x485c7e - the zoo
      // zoo.ini [user] lastfile names, nothing when it names none)
      const bool dialog = action == UiAction::STARTUP_LOAD_GAME;
      path = dialog ? SaveGame::askLoadPath() : SaveGame::lastFile();
      if (!path.empty() && gameLoad(&resource_manager, path)) {
        if (dialog)
          SaveGame::setLastFile(path); // (loadZooDialog 0x4cc0d7)
        delete layout;
        layout = nullptr;
        g_currentState = LayoutState::GAME_LOOP;
      }
      break;
    }
    // Save Game: the Windows "Save a zoo..." dialog, as the original; once
    // it saved, Game Options closes (zoo.exe gameopt1_SaveGame_click 0x477041
    // -> 0x477004: nothing while Save Game is switched off; uiFind 5 hidden)
    case UiAction::GAME_SAVE:
      saveGameClicked();
      break;
    // Load Game: asked to save first (askSaveFirst); then "Load a zoo...",
    // its map started afresh, then the save
    case UiAction::GAME_LOAD: {
      if (!queuedAction && askSaveFirst(&resource_manager, action))
        break;
      const std::string path = SaveGame::askLoadPath();
      if (!path.empty() && gameLoad(&resource_manager, path))
        SaveGame::setLastFile(path); // (zoo.exe loadZooDialog 0x4cc0d7)
      break;
    }
    // The game menu: Exit Game quits; Main Menu leaves the game - each asked
    // to save first (askSaveFirst)
    case UiAction::GAME_EXIT:
      if (!queuedAction && askSaveFirst(&resource_manager, action))
        break;
      running = false;
      break;
    case UiAction::GAME_MAIN_MENU:
      if (!queuedAction && askSaveFirst(&resource_manager, action))
        break;
      destroyHud();
      [[fallthrough]];
    // (zoo.exe scenario_BackToMainMenu_click 0x48a99f: the scenario list's
    // slot 0x1c called, the panel uiFind 0x20 hidden and the start-up screen,
    // 0x1a, shown)
    case UiAction::CREDITS_EXIT:
    case UiAction::SCENARIO_BACK_TO_MAIN_MENU: {
      // (the Mod Menu is opened from the Game Options' Mods & Updates: its
      // Back goes back there)
      bool backToOptions = g_currentState == LayoutState::MOD_MENU && Features::optionsWindow;
      delete layout;
      lyt_reader = resource_manager.getIniReader(
          getLayoutPath(&resource_manager, "startup.lyt"));
      layout = new UiLayout(lyt_reader, &resource_manager);
      hookMainMenu(layout, &resource_manager);
      g_currentState = LayoutState::MAIN_MENU;
      if (backToOptions && g_menuOptions)
        g_menuOptions->open(false, UiOptions::kModsPage);

      g_scenarioListBox = nullptr;
      g_freeformListBox = nullptr;
      g_scenarioDescText = nullptr;
      g_freeformDescText = nullptr;
      g_scenarioMap = nullptr;
      g_freeformMap = nullptr;
      break;
    }

    case UiAction::SCENARIO_LIST_SELECTION:
      updateScenarioDetails(layout, g_scenarioManager, &resource_manager);
      break;

    // (zoo.exe mapselec_MapList_click 0x4ca58e: a click that picks a different
    // row shows its map, 0x50f396; one that picks none keeps the row)
    case UiAction::FREEFORM_LIST_SELECTION:
      updateFreeformDetails(layout, g_scenarioManager, &resource_manager);
      break;

    case UiAction::CASH_SPINNER_UP:
      stepStartingCash(layout, 1);
      break;

    case UiAction::CASH_SPINNER_DOWN:
      stepStartingCash(layout, -1);
      break;

    // (zoo.exe scenario_StartScenario_click 0x4c9d83 -> 0x4c9bc9(0): the row
    // chosen started, [user] lastfile cleared)
    case UiAction::PLAY_SCENARIO_START:
      // (in the Mod Menu that button is Switch on / off, or Download)
      if (g_currentState == LayoutState::MOD_MENU) {
        ModMenu::toggle(layout, &resource_manager);
        break;
      }
      if (g_scenarioListBox && g_scenarioListBox->getSelectedIndex() >= 0) {
        int idx = g_scenarioListBox->getSelectedIndex();
        const ScenarioInfo *info = g_scenarioManager->getScenario(idx);
        if (info && !info->isLocked) {
          SDL_Log("Starting Scenario: %s", info->name.c_str());
          startScenario(&resource_manager, *info);
          SaveGame::setLastFile(""); // (zoo.exe 0x4c9bc9)

          delete layout;
          layout = nullptr; // the in-game HUD replaces the menus
          g_currentState = LayoutState::GAME_LOOP;
        }
      }
      break;

    // (zoo.exe mapselec_PlayMap_click 0x4ca34e: startMap 0x4ca15d with the
    // starting cash chosen, the map screen's panels, uiFind 0x27 and 0x1a,
    // hidden and the game begun)
    case UiAction::PLAY_FREEFORM_START:
      if (g_freeformListBox && g_freeformListBox->getSelectedIndex() >= 0) {
        int idx = g_freeformListBox->getSelectedIndex();
        const FreeformMap *map = g_scenarioManager->getFreeformMap(idx);
        if (map) {
          SDL_Log("Starting Freeform: %s", map->name.c_str());
          g_world->loadFreeform(map->path);
          g_currentMapPath = map->path;
          SaveGame::setLastFile(""); // (zoo.exe mapselec_PlayMap_click -> 0x4ca1f0)
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
    // (zoo.exe main_RotateCW_click 0x4b071a, main_RotateCCW_click 0x416cb7:
    // the view's turn + 1, or + 3, of four - 0x4b06be)
    case UiAction::HUD_ROTATE_CW:
      g_world->rotateView(+1);
      break;
    case UiAction::HUD_ROTATE_CCW:
      g_world->rotateView(-1);
      break;
    // (zoo.exe main_PauseButton_click 0x59c7a4 -> pauseGame 0x59c6b8: Pause hidden,
    // Play shown in its place, the game paused; Play the other way round)
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

    FrameProfile::phase("input dispatch");
    // UPDATE LOGGING
    if (g_currentState == LayoutState::GAME_LOOP) {
      // SDL_Log("MainLoop: Pre-Update");
      const bool *state = Embed::keyboardState();
      static const bool noKeys[SDL_SCANCODE_COUNT] = {};
      // Game Options (window or original panel), questions and naming boxes
      // stop the whole simulation. Only resume a game that the modal paused;
      // a zoo the player had already paused must stay paused on dismissal.
      bool dialogUp = gameModalOpen();
      if (dialogUp && !g_world->isPaused()) {
        setGamePaused(true);
        g_dialogPaused = true;
      } else if (!dialogUp && g_dialogPaused) {
        g_dialogPaused = false;
        if (!g_popupPaused)
          setGamePaused(false);
        // ours: native dialogs can block the thread. Their elapsed time is
        // menu time, never a simulation catch-up interval on dismissal.
        g_speedClock.clearPending();deltaTime=0;lastFrameTime=SDL_GetTicks();
      }
      if(g_resetSimulationFrame) {
        g_resetSimulationFrame=false;g_speedClock.clearPending();deltaTime=0;lastFrameTime=SDL_GetTicks();
      }
      bool controlsSent=false;
      const bool *frameKeys=g_console.isOpen() || UiText::isEditing() || dialogUp?noKeys:state;
      auto advanceGame=[&](float simulationSeconds,float inputSeconds) {
      // (the date a building put down now opens on: ZTBuilding +0x180)
      g_world->objectsMutable().today = g_sim.today();
      // (and a tank filter's: ZTTankFilter +0x178, +0x180)
      g_world->getFences().today = g_sim.today();
      g_world->update(controlsSent?noKeys:frameKeys,simulationSeconds,inputSeconds);
      controlsSent=true;
      // The clock runs while the game isn't paused
      g_sim.unlimited = Features::infiniteMoney; // (the Mod Menu's cheat)
      if (!g_world->isPaused() && simulationSeconds>0) {
        g_sim.update(simulationSeconds);
        // A private foundation's donation, under $1,000 (zoo.exe
        // zooPeriodicTick 0x4351a9, foundationDonation 0x613e4a): "A private
        // foundation has donated %s to your zoo." (15002, kind 4: green) or
        // "No foundations willing..." (15003, kind 5: red)
        {
          int given = 0;
          if (int news = g_sim.foundationTick(given)) {
            std::string m = resource_manager.getString(news);
            if (size_t at = m.find("%s"); at != std::string::npos)
              m.replace(at, 2, formatMoney(given));
            g_hud->postMessage(m, news == 15002 ? 4 : 5);
            updateHudClock(&resource_manager);
          }
        }
        g_goals.update(simulationSeconds);
        // (the scenario's end taken once, then cleared: takeWonFlag 0x4c9bb2)
        if (const int outcome = g_scenarioOutcome) {
          g_scenarioOutcome = 0;
          scenarioComplete(&resource_manager, outcome);
        }
        // Research: programs finished, their effects, and the news
        // ("Research & Conservation: %r is now available.", 23020; "%s: All
        // selected programs have been completed.", 23021)
        Research::Tick rt = Research::get().advance(
            simulationSeconds, [](int branch, double amount) { return g_sim.payTick(ZooSim::Research, branch, amount); });
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
        // The advisor, each [checks] message ms (zoo.exe zooAdvisories
        // 0x420347), on the zoo's counts (zooRatingUpdate 0x41f881): more
        // than its share of the animals sick, "Protestors are demonstrating
        // ..." (10009) - then, with more than 10 guests, the sick animals
        // (10008); hungry, thirsty, restroom, gift and tired guests
        // (10011-10015); litter in hand, "...too dirty." (10016) - each in
        // red; guests on average very happy (10021, green) or not (10022).
        // Then the money, the cash rounded: $1-$1,000 "Your zoo is running
        // low on money." (10018); none "Your zoo has run out of money."
        // (10019); below none also "...Funding has stopped for all
        // programs." (10020)
        static double adviceMs = 0;
        adviceMs += simulationSeconds * 1000.0;
        if (adviceMs > Original::economy().adviceEveryMs) {
          adviceMs = 0;
          const Original::Economy &econ = Original::economy();
          const Guests::Census gc = g_world->getGuests().census();
          int animals = 0, sick = 0;
          for (const Animals::Member &m : g_world->getAnimals().members()) {
            animals++;
            sick += m.sick ? 1 : 0;
          }
          (void)econ;
          for (auto [id, kind] : zooAdvice(gc, animals, sick))
            g_hud->postMessage(resource_manager.getString(id), kind);
        }
        // (the money's, not with the infinite money cheat on)
        static double moneyAdviceMs = 0;
        moneyAdviceMs += simulationSeconds * 1000.0;
        if (moneyAdviceMs > Original::economy().adviceEveryMs && !g_sim.unlimited) {
          moneyAdviceMs = 0;
          const long cash = std::lround(g_sim.cash());
          if (cash > 0 && cash < 1001)
            g_hud->postMessage(resource_manager.getString(10018), 2);
          if (cash <= 0)
            g_hud->postMessage(resource_manager.getString(10019), 2);
          if (cash < 0)
            g_hud->postMessage(resource_manager.getString(10020), 2);
        }
      }
      resumeAfterPopup();
      // Tank filters bill each time they clean (zoo.exe filterPayUpkeep
      // 0x49bec7: its cost spent and booked as the buildings' upkeep)
      if (int upkeep = g_world->getFences().takeUpkeep())
        g_sim.spend(ZooSim::Upkeep, upkeep);
      if (g_sim.changed()) {
        // A new month can unlock things to buy
        // Filters: this month's upkeep becomes last month's
        int months = static_cast<int>(g_sim.history().size());
        if (months != g_world->getFences().month()) {
          if (g_world->getFences().month() > 0) {
            g_world->getFences().newMonth();
            // The shows' months roll too (zoo.exe monthlyUpkeep 0x483f11 -
            // rollAllMonthly 0x4842a2 - rollMonthlyB 0x48b57e)
            g_world->getShows().newMonth();
            // Staff are paid every month (zoo.exe monthlyUpkeep 0x483f11:
            // each one's wage, booked by kind - keepers and the DRT on
            // bookExpense360 0x5ad038, tour guides on 0x48bd8b,
            // maintenance workers on bookUpkeep 0x483d34 - all Employee
            // Wages on Finances)
            if (int wages = g_world->getStaff().monthlyWages())
              g_sim.spend(ZooSim::Wages, wages);
            // Members: each may lapse (rand 0-100 less cMembershipJoinFactor
            // above the zoo rating); the rest give cEndowmentGiftLow to
            // cEndowmentGiftHigh each (Private Donations): zoo.exe monthlyUpkeep
            // 0x483f11 - the lapses booked first (0x5ad160), then one roll for
            // them all, (rand % 101 x (high - low) x 0.01 (0x635048) + low) x
            // members, on the Donations row (0x48442b, row 20)
            {
              const Original::Economy &e = Original::economy();
              Guests &gs = g_world->getGuests();
              int stay = 0;
              for (int m = 0; m < gs.members; m++)
                stay += std::rand() % 101 - e.membershipJoinFactor > static_cast<int>(g_sim.rating()) ? 0 : 1;
              gs.members = stay;
              if (stay > 0)
                g_sim.earn(ZooSim::Donations, stay * (e.endowmentGiftLow + (std::rand() % 101) *
                                                      (e.endowmentGiftHigh - e.endowmentGiftLow) * 0.01));
            }
            // And every building's upkeep (its cUpkeep, Zoo Upkeep Cost:
            // zoo.exe monthlyUpkeep 0x483f11 sums buildingMonthlyUpkeep
            // 0x59e17c and books it once, bookExpense690 0x49bd80, row 28)
            for (PlacedObjects::Object &o : g_world->objectsMutable().all()) {
              if (o.label.empty())
                continue;
              // (this month's visitors become last month's: zoo.exe
              // rollMonthlyCounters 0x4842b7, +0x198 into +0x194, for each
              // building - rollAllMonthly 0x4842a2)
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
        int month = unlockMonthFor(monthsPlayed());
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
      return !g_world->isPaused();
      };
      if(gameBenchmark.enabled())gameBenchmark.beforeSimulation();
      syncGameSpeedControl();
      g_speedClock.advance(deltaTime,g_gameSpeed,g_world->isPaused(),advanceGame);
      if(gameBenchmark.enabled())gameBenchmark.afterSimulation();
      // SDL_Log("MainLoop: Post-Update");

      FrameProfile::phase("simulation");
      // SDL_Log("MainLoop: Pre-Draw");
      // (the zoo at the UI's scale: 1:1 as the original, or the uiscale
      // extra's whole steps)
      g_world->setView(gameViewRect(window.renderer), getUiTransform(window.renderer).scale);
      g_world->draw(window.renderer);
      // addition: the graphics effects picked (GraphicsEffects.hpp), on the
      // map only - before the bars and windows go over it
      GraphicsEffects::applyMap(window.renderer);
      if (g_snapshot == 1) { // (the map alone)
        g_snapshot = 0;
        takeSnapshot(window.renderer, &resource_manager);
      }
      updateZoomButtons(g_world); // the mouse wheel zooms too
      if (Embed::snapAsked() && g_hud) // (the Lab's Play: snap)
        g_hud->tooltipShot = true;
      drawHud(window.renderer, &resource_manager);
      drawToolCost(window.renderer, &resource_manager);
      if (g_hud)
        drawOptions(window.renderer, &resource_manager, g_hud->options);
      drawDialogOverOptions(window.renderer, &resource_manager);
      if (g_snapshot == 2) { // (the screen as it is)
        g_snapshot = 0;
        takeSnapshot(window.renderer, &resource_manager);
      }
      {
        int ow = 0, oh = 0;
        SDL_GetCurrentRenderOutputSize(window.renderer, &ow, &oh);
        g_console.draw(window.renderer, &resource_manager, ow, oh);
      }
      // SDL_Log("MainLoop: Post-Draw");
    } else if (layout) {
      // addition: read Later/Escape/quit and navigation before the updater may
      // hand off an accepted download. Never start it over a zoo or on exit.
      if (g_currentState == LayoutState::MAIN_MENU) {
        if (running) updateUi.tick();
        else updateUi.dismiss();
        updateUi.prepareFooter(layout);
      } else updateUi.dismiss();
      drawLayoutCentered(window.renderer, layout, &resource_manager);
      drawOptions(window.renderer, &resource_manager, g_menuOptions); // (over the menu)
      if (g_currentState == LayoutState::MAIN_MENU && !(g_menuOptions && g_menuOptions->isOpen())) {
        UiTransform t = getUiTransform(window.renderer);
        drawUiScaled(window.renderer, &resource_manager, t, [&](SDL_Renderer *r) {
          SDL_Rect viewport = {static_cast<int>(std::lround(t.offsetX / t.scale)),
                               static_cast<int>(std::lround(t.offsetY / t.scale)), UI_LAYOUT_W, UI_LAYOUT_H};
          SDL_SetRenderViewport(r, &viewport);
          updateUi.draw(r, layout);
          SDL_SetRenderViewport(r, nullptr);
        });
      }
    }

    FrameProfile::phase("rendering");
    // ours: the zoo's species' animations loaded ahead, one at a time on
    // another thread (Animals::warmOne: the one done collected, the next
    // started). They were read here in the frame's spare time - up to 150 ms
    // of it - and never on a PC without that much to spare
    if (g_world && g_currentState == LayoutState::GAME_LOOP)
      g_world->getAnimals().warmOne();
    if(gameBenchmark.enabled())gameBenchmark.beforePresent(*g_world,window.renderer);
    window.present();FrameProfile::phase("present");
    if(gameBenchmark.enabled() && gameBenchmark.afterPresent(*g_world,g_speedClock.pending(),g_speedClock.discarded()))running=0;
    // ours: confirm to the external updater only after a new ready menu frame.
    static bool updateAcknowledged = false;
    if (!updateAcknowledged && g_currentState == LayoutState::MAIN_MENU) {
      Updater::acknowledgeLaunch(argc, argv);
      updateAcknowledged = true;
    }
    FrameProfile::end();

    // Check if it's time for periodic memory dump
    // ZT_MEMORY_DUMP_CHECK();
  }

  // Later is an explicit deferral, not permission to install on exit.
  Updater::atExit();

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
