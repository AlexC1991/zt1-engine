#ifndef UI_OPTIONS_HPP
#define UI_OPTIONS_HPP

#include <functional>
#include <map>
#include <set>
#include <string>
#include <vector>

#include <SDL2/SDL.h>

#include "../Input.hpp"
#include "UiAction.hpp"

class ResourceManager;
class UiLayout;
class UiButton;
class UiScrollBar;
class UiText;

// Interface Size's slider (Game Options; zoo.ini [user] uiScale,
// RenderSettings::uiScaleSetting), for the Options window and the original
// side panel alike. Any size from half the original's (0.5x) to the largest
// the window can show (every screen still fitting the original's 800x600),
// in 0.05 steps, and an Auto tick box: the window's own size (1:1, whole
// steps up on a big window). Dragged in the Options window, the rest of the
// interface grows and shrinks as the thumb goes while the window itself
// keeps its size (RenderSettings::uiScaleHeld) till the mouse is let go - so
// the slider stays under it. In the original side panel, which grows with
// the rest, a size is tried once the thumb rests a moment, the mouse measured
// against the track where it was when taken. Kept on letting go.
class UiScaleControl {
public:
  // The slider, the text that says the size ("Interface Size: 1.5x"), the
  // Auto tick box and its words ("Auto (1x)") - either may be null - and
  // whether the slider's own window holds its size while dragged
  void attach(UiScrollBar *bar, UiText *value, UiButton *autoBox, UiText *autoText, const std::string &prefix,
              bool holdsWindow);
  // Each frame before the slider draws. originX/Y: where the slider's
  // layout units start in the window, in pixels
  void update(SDL_Renderer *renderer, float originX = 0.0f, float originY = 0.0f);

private:
  UiScrollBar *bar = nullptr;
  UiText *value = nullptr, *note = nullptr;
  UiButton *autoBox = nullptr;
  bool holdsWindow = false;
  int lastW = 800, lastH = 600; // (the window, as update() last saw it)
  std::string prefix;
  float pending = -1.0f;
  bool drag = false;
  float dragX = 0.0f, dragW = 1.0f;
  int at = -1;
  Uint32 atSince = 0;
  void show(float scale);
};

// The Game Options window (an addition, the optionswindow extra; off: the
// original's side panel). A window in the middle of the screen made from
// the game's own Help window (ui/help/help: a green frame, a column on the
// left, a page on the right - stretched from its edges to 640 x 440): the
// pages listed down the left - Game (in a zoo only: Load, Save, Main Menu,
// Exit), Display, Graphics, Sound, Controls, Accessibility, Gameplay (the
// extras), Mods & Updates - and each page's settings on the right, what the
// one under the mouse does written underneath. Choices and on/offs are the
// game's own tick boxes. Opened from the in-game disk button and the main
// menu's Game Options. Every setting is zoo.ini's (DisplaySettings.hpp's
// for the new ones) and works the moment it's changed.
class UiOptions {
public:
  explicit UiOptions(ResourceManager *rm);
  ~UiOptions();

  // The Mods & Updates page (open()'s page: the Mod Menu's Back comes there)
  static constexpr int kModsPage = 8;
  // page: the one shown first (-1: Game in a zoo, else Display)
  void open(bool inGame, int page = -1);
  void close() { this->closeAsked = true; }
  bool isOpen() const { return this->layout != nullptr; }

  // While it's open it takes every input (it's modal); the Game page's
  // buttons give back GAME_LOAD / GAME_SAVE / GAME_MAIN_MENU / GAME_EXIT
  // (the window closes first). Inputs in the layout units it's drawn in
  UiAction handleInputs(std::vector<Input> &inputs);
  // Centred in screen (layout units); originX/Y as UiScaleControl's
  void draw(SDL_Renderer *renderer, SDL_Rect *screen, float originX = 0.0f, float originY = 0.0f);

  // The tooltip timing changed (the game screen keeps its own copy)
  std::function<void(int delay, int durationMs)> onTooltips;
  // Reset window positions (Controls): the dragged windows put back
  std::function<void()> onResetWindows;
  // Back in the middle (Reset window positions)
  void resetPosition();

private:
  ResourceManager *rm;
  UiLayout *layout = nullptr;
  bool inGame = false, closeAsked = false;
  bool justOpened = false; // (its first frame: the Esc that opened it isn't its own)
  int page = 1;
  UiAction pending = UiAction::NONE;
  UiScaleControl scale;
  // What each element does, shown under the page when the mouse is on it
  std::map<int, std::string> about;
  int mouseX = -1, mouseY = -1;
  // Ours, the dragwindows extra: moved from the middle by its title (zoo.ini
  // [Windows] wOptions), and its lock beside the close button (lOptions)
  int offsetX = 0, offsetY = 0;
  bool locked = false, lockDown = false, dragging = false, dragMoved = false;
  int dragFromX = 0, dragFromY = 0, dragOffX = 0, dragOffY = 0;
  SDL_Rect lastRect = {0, 0, 0, 0}, lockRect = {0, 0, 0, 0};
  bool settled = false; // (its place read from zoo.ini)
  // Rows left out while they don't apply (Art Sharpness without FSR, the
  // filter's strength with it off, empty mod rows)
  std::set<int> hiddenIds;
  // Display's resolutions (smallest first), the one dragged to till it's
  // let go, and the window mode they were listed for
  std::vector<SDL_Point> resolutions;
  int resolutionPending = -1, listedMode = -1;
  void setPage(int page);
  void wire();
  void finishClose();
  void hideRow(std::initializer_list<int> ids, bool hide);
  void showResolutions();
  void showMods();
  // A mod's own settings (its mod.json "settings"): the page for the mod
  // whose Settings button was pressed (its folder)
  std::string modShown;
  void showUpdates();
  void showModSettings();
  // The Shaders page for the shader mods switched on: the list, and the
  // shown one's settings
  int shaderShown = 0;
  void showShaders();
  // Each frame: what changes while it's open (the resolution let go, the
  // art's note, rows shown or left out)
  void tick(SDL_Renderer *renderer);
};

#endif // UI_OPTIONS_HPP
