#include "UiOptions.hpp"

#include <algorithm>
#include <cmath>
#include <cctype>
#include <cstdio>

#include "../Animation.hpp"
#include "../CompassDirection.hpp"
#include "../DisplaySettings.hpp"
#include "../Embed.hpp"
#include "../Features.hpp"
#include "../GpuFsr.hpp"
#include "../HdArt.hpp"
#include "../ShaderMods.hpp"
#include "../IniReader.hpp"
#include "../InputManager.hpp"
#include "../ModMenu.hpp"
#include "../Mods.hpp"
#include "../Updater.hpp"
#include "../RenderSettings.hpp"
#include "../ResourceManager.hpp"
#include "../Settings.hpp"
#include "../Window.hpp"
#include "UiButton.hpp"
#include "UiLayout.hpp"
#include "UiScrollBar.hpp"
#include "UiText.hpp"
#include "WindowLock.hpp"

extern bool g_showDiagnostics; // (Window.cpp: F3's FPS counter)
extern float g_ZoomLevel;      // (World.cpp: the map's zoom)

// ============================================================================
// Interface Size's slider
// ============================================================================
namespace {
// (a size as the label shows it: "1.35x")
std::string scaleName(float scale) {
  char b[16];
  std::snprintf(b, sizeof b, "%.2fx", scale);
  std::string s = b;
  // (no trailing zeros: 1.50x -> 1.5x, 1.00x -> 1x)
  size_t dot = s.find('.');
  size_t end = s.find_last_not_of('0', s.size() - 2);
  if (end != std::string::npos && end >= dot)
    s = s.substr(0, end == dot ? dot : end + 1) + "x";
  return s;
}
// (the slider's 0..100 to a size from the smallest to the largest the
// window can show, in RenderSettings::kUiScaleStep steps, and back)
float scaleAt(int v, float top) {
  float s = RenderSettings::kUiScaleMin + v / 100.0f * (top - RenderSettings::kUiScaleMin);
  s = std::round(s / RenderSettings::kUiScaleStep) * RenderSettings::kUiScaleStep;
  return std::clamp(s, RenderSettings::kUiScaleMin, top);
}
int valueOf(float scale, float top) {
  if (top <= RenderSettings::kUiScaleMin)
    return 0;
  return std::clamp(static_cast<int>(std::lround((scale - RenderSettings::kUiScaleMin) /
                                                 (top - RenderSettings::kUiScaleMin) * 100.0f)),
                    0, 100);
}
void windowPixels(SDL_Renderer *renderer, int &w, int &h) {
  // (the window's own size: while the UI draws into FSR's layer the
  // renderer's output is that layer, already scaled down)
  w = 800;
  h = 600;
  if (SDL_Window *win = SDL_RenderGetWindow(renderer))
    SDL_GetWindowSizeInPixels(win, &w, &h);
}
} // namespace

void UiScaleControl::attach(UiScrollBar *bar, UiText *value, UiButton *autoBox, UiText *autoText,
                            const std::string &prefix, bool holdsWindow) {
  this->bar = bar;
  this->value = value;
  this->autoBox = autoBox;
  this->note = autoText;
  this->prefix = prefix;
  this->holdsWindow = holdsWindow;
  this->drag = false;
  this->pending = -1.0f;
  if (!bar)
    return;
  // (pressed or dragged: update() has it, and keeps the size it's let go on)
  bar->onChange = [](int) {};
  if (autoBox)
    autoBox->onClick = [this] {
      // Auto ticked: the size from the window again; unticked: the size it
      // was, as a size of its own
      float now = RenderSettings::uiScaleFor(this->lastW, this->lastH);
      RenderSettings::uiScaleSetting = this->autoBox->isToggledOn() ? 0.0f : now;
      char b[16];
      std::snprintf(b, sizeof b, "%g", RenderSettings::uiScaleSetting);
      Settings::set("user", "uiScale", b);
    };
}

void UiScaleControl::show(float scale) {
  std::string t = this->prefix + scaleName(scale);
  if (this->value && this->value->getText() != t)
    this->value->setText(t);
}

void UiScaleControl::update(SDL_Renderer *renderer, float originX, float originY) {
  (void)originY;
  if (!this->bar)
    return;
  int ow, oh;
  windowPixels(renderer, ow, oh);
  this->lastW = ow;
  this->lastH = oh;
  // From the smallest (half the original's) to the largest this window can
  // show (every screen still fitting the original's 800x600)
  const float top = RenderSettings::uiScaleMax(ow, oh);
  bool held = this->bar->isDragging();
  if (held) {
    Uint32 now = SDL_GetTicks();
    if (!this->drag) {
      this->drag = true;
      this->at = -1;
      if (this->holdsWindow) {
        // (the Options window keeps the size it has till it's let go - the
        // slider stays under the mouse - while the rest grows and shrinks)
        RenderSettings::uiScaleHeld = RenderSettings::uiScaleFor(ow, oh);
      } else {
        // (the original panel grows with the rest: the mouse measured
        // against the track where it was when taken, in window pixels)
        float sc = RenderSettings::uiScaleFor(ow, oh);
        SDL_Rect t = this->bar->trackRect();
        this->dragX = originX + t.x * sc;
        this->dragW = std::max(1.0f, t.w * sc);
      }
    }
    int v = this->bar->getValue();
    if (!this->holdsWindow) {
      // (the mouse as its events say: a drag made of events, as the Lab's,
      // works as a real one)
      float along = std::clamp((MouseEvents::x - this->dragX) / this->dragW, 0.0f, 1.0f);
      v = static_cast<int>(std::lround(along * 100));
      this->bar->setValue(v);
    }
    float s = scaleAt(v, top);
    int step = static_cast<int>(std::lround(s / RenderSettings::kUiScaleStep));
    if (step != this->at) {
      this->at = step;
      this->atSince = now;
    }
    // At once, as the thumb goes - or, the original panel growing under the
    // mouse, once it rests a moment on a size
    if (this->holdsWindow || now - this->atSince >= 300)
      RenderSettings::uiScaleSetting = s;
    this->show(s);
    this->pending = s; // (kept in zoo.ini on letting go, below)
  } else {
    this->drag = false;
  }
  if (this->pending >= 0.0f && !held) {
    // Let go (or a click on the bar): at work, kept - and the Options window
    // takes the size too
    RenderSettings::uiScaleSetting = this->pending;
    RenderSettings::uiScaleHeld = 0.0f;
    char b[16];
    std::snprintf(b, sizeof b, "%g", this->pending);
    Settings::set("user", "uiScale", b);
    this->pending = -1.0f;
  }
  float atWork = RenderSettings::uiScaleFor(ow, oh);
  if (!this->drag) {
    // (the thumb on the size at work - Auto's too, and after the window's
    // resized)
    this->bar->setValue(valueOf(atWork, top));
    this->show(atWork);
  }
  if (this->autoBox)
    this->autoBox->setToggledOn(RenderSettings::uiScaleSetting <= 0.0f && !this->drag);
  if (this->note) {
    // (what Auto comes to in this window: 1:1, whole steps up on a big one)
    float autoScale = RenderSettings::uiScaleAuto(ow, oh);
    std::string b = "Auto (" + scaleName(autoScale) + ")";
    if (this->note->getText() != b)
      this->note->setText(b);
  }
}

// ============================================================================
// The Options window's layout (ui/zt1options.lyt, made here)
// ============================================================================
namespace {
// Pages, down the left
// (the last, a mod's own settings, has no tab: Mods & Updates' Settings
// buttons open it, its Back goes back)
enum Page { kGame, kDisplay, kGraphics, kSound, kControls, kAccess, kShaders, kGameplay, kMods, kModSet, kPages };
const int kTabs = kModSet;
static_assert(kMods == UiOptions::kModsPage, "the Mods & Updates page's number");
const char *kPageNames[kPages] = {"Game",          "Display", "Graphics", "Sound",          "Controls",
                                  "Accessibility", "Shaders", "Gameplay", "Mods & Updates", "Mod settings"};
const char *kPageAbout[kPages] = {
    "Load or save your zoo, or leave it.",
    "The game's window: windowed or full screen, its resolution, frame rate and brightness.",
    "How big the interface is drawn, and how the art looks drawn bigger.",
    "How loud the game and the menu music are.",
    "Scrolling the map, tool tips, hotkeys and which Game Options you get.",
    "Colour-blind filters and a bigger mouse pointer.",
    "Shaders your mods bring - a toon look, colour grading... Each on or off, and its settings.",
    "The engine's additions to how the game plays. Off, each does what the original game does.",
    "Your mods, each on or off, and whether the game looks for updates.",
    "This mod's own settings (its mod.json). Each works the moment it's changed.",
};
// Element ids: the frame's, the tabs', then each page's own (9200 + 100 a page)
enum : int {
  kBackground = 9100, kTitle, kClose, kPageTitle, kAbout, kDone,
  kTabFirst = 9110, kTabSet = 9119,
};
int pageId(int page, int k) { return 9200 + page * 100 + k; }
int pageOf(int id) { return id >= 9200 && id < 9200 + kPages * 100 ? (id - 9200) / 100 : -1; }
// Each page's own (k): its rows' pieces. A choice is a tick box and its
// words (k, k + 1) in a set
enum : int { kLoad = 1, kSave, kMainMenu, kExit }; // Game
enum : int {
  kModeLabel = 1, kModeWin, kModeBorder = 4, kModeFull = 6, kModeSet = 8,
  kResLabel, kResBar, kFpsLimLabel, kFpsLimBar, kVsync, kVsyncText = 14,
  kBrightLabel, kBrightBar, kFps, kFpsText = 18,
}; // Display
enum : int {
  kScaleLabel = 1, kScaleBar, kScaleAuto, kScaleNote,
  kArtLabel, kArtFsr, kArtSharp = 8, kArtSmooth = 10, kArtMmpx = 12, kArtSet = 14,
  kArtNote, kSharpLabel, kSharpBar, kArtStyled = 18,
}; // Graphics
enum : int { kVolumeLabel = 1, kVolumeBar, kMusicLabel, kMusicBar }; // Sound
enum : int {
  kScrollLabel = 1, kScrollBar, kEdge, kEdgeText, kTipsLabel, kTipsShort, kTipsLong = 8, kTipsSet = 10,
  kDelayLabel, kDelayBar, kDurationLabel, kDurationBar, kStyleLabel, kStyleNew, kStyleOld = 18, kStyleSet = 20,
  kWinLabel, kWinReset,
}; // Controls
enum : int {
  kFilterLabel = 1, kFilterOff, kFilterP = 4, kFilterD = 6, kFilterT = 8, kFilterSet = 10,
  kStrengthLabel, kStrengthBar, kCursorLabel, kCursor1, kCursor2 = 16, kCursor3 = 18, kCursorSet = 20,
}; // Accessibility
// (Shaders: each shader a tick box, its name and its Settings button, from
// kShaderFirst + 3i; the shown one's settings a label and slider each, from
// kSettingFirst + 2j, three across)
enum : int { kShaderFirst = 1, kShaderRows = 3, kSettingFirst = 20, kSettingRows = 12, kShadersNone = 50 };
// (Mods & Updates: each mod a tick box and its name, kModFirst + 2i, + 1,
// and its Settings button when it has some, kModSettingsFirst + i)
enum : int { kModMenu = 1, kModMenuText, kModsTitle, kModsNote, kModPrev, kModNext, kModPageText, kUpdateNow = 12, kModFirst = 20, kModRows = 5, kModSettingsFirst = 80 };
// (Mods & Updates' Check for updates row: which of the page's switches it is)
int updatesRow();
// (a mod's settings page: Back, its name and what it is; each setting from
// kMsFirst + 4j - its words, its slider, a toggle's tick box and words -
// three across)
enum : int { kMsBack = 1, kMsName, kMsNone, kMsFirst = 10, kMsRows = 12 };
enum : int { kSwitchFirst = 40, kSwitchTextFirst = 70 }; // a page's extras: toggle, its name
// The window's size, and its columns: labels, then controls
const int kW = 640, kH = 440;
const int kLabelX = 196, kControlX = 356, kControlW = 254, kRowsY = 58;
// (whether the window being made has the Game page: a zoo's open)
bool g_makingInGame = true;

// Which page each extra is on (any other: Gameplay). This window's own
// switch is Controls' Game Options row, not a tick box of its own
int extraPage(const std::string &name) {
  if (name == "optionswindow")
    return -1;
  if (name == "hotkeys" || name == "smartstack" || name == "dragwindows")
    return kControls;
  if (name == "mods" || name == "modmenu" || name == "updatechecks")
    return kMods;
  return kGameplay;
}
std::string extraName(const std::string &name) {
  static const std::map<std::string, std::string> names = {
      {"allunlocked", "All items unlocked"}, {"fencemodes", "Fence drag modes"},
      {"staffpaths", "Staff keep to paths"}, {"smoothroutes", "Smooth walking"},
      {"maptooltips", "Map hints"},          {"elevatedpaths", "Raised walkways"},
      {"pathmodes", "Path drag modes"},      {"hotkeys", "Extra hotkeys"},
      {"mods", "Load mods"},                 {"modmenu", "Mod Menu button"},
      {"updatechecks", "Check for updates"}, {"misnamedart", "Misnamed art"},
      {"optionswindow", "This Options window"}, {"smartstack", "Windows move aside"},
      {"dragwindows", "Drag any window"},       {"infocloseslists", "Windows close lists"}};
  auto it = names.find(name);
  return it == names.end() ? name : it->second;
}

// A description as the box under the page shows it: its first letter a
// capital, broken into lines that fit the box (UiText wraps at 55
// characters, wider than the box)
std::string aboutText(std::string s) {
  if (!s.empty())
    s[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(s[0])));
  std::string out, line, word;
  auto flush = [&] {
    if (!line.empty())
      out += (out.empty() ? "" : "\n") + line;
    line.clear();
  };
  for (size_t i = 0; i <= s.size(); i++) {
    if (i == s.size() || s[i] == ' ') {
      if (!word.empty() && line.size() + 1 + word.size() > 54)
        flush();
      if (!word.empty())
        line += (line.empty() ? "" : " ") + word;
      word.clear();
    } else {
      word += s[i];
    }
  }
  flush();
  return out;
}

// What each extra does, short enough for the box under the page (its long
// form, with what "off" means, is Features.hpp's for the dev console)
std::string extraAbout(const Features::Switch &f) {
  static const std::map<std::string, std::string> about = {
      {"allunlocked", "Freeform: every item from the start. Off: released month by month."},
      {"fencemodes", "Fence drags in a line, box or grid; Tab changes. Off: follows the mouse."},
      {"staffpaths", "Staff walk the paths and wait on them. Off: they roam open ground."},
      {"smoothroutes", "Walkers take straight lines where the way is clear. Off: tile by tile."},
      {"maptooltips", "Tool hints over the map, not just on buttons."},
      {"elevatedpaths", "Raised walkways and stairs; the mouse wheel sets the height."},
      {"pathmodes", "Path drags bend once; Tab changes. Off: the path follows the mouse."},
      {"hotkeys", "Esc and right-click put tools away, Ctrl+Z undoes, Q / E turn, Alt+Enter or F11 full screen."},
      {"mods", "Mods switched on in the Mod Menu load over the game's files."},
      {"modmenu", "The Mod Menu button above. Off: no Mod Menu, as the original."},
      {"updatechecks", "An installed copy looks for a new release at start and asks first."},
      {"misnamedart", "Marine Mania art the game's files misname is found and shown."},
      {"optionswindow", "This window, also on the main menu. Off: the original side panel."}};
  auto it = about.find(f.name);
  return it == about.end() ? f.about : it->second;
}

std::string section(const std::string &name, const std::string &body) {
  return "[" + name + "]\r\n" + body + "\r\n";
}
std::string at(int x, int y) { return "x=" + std::to_string(x) + "\r\ny=" + std::to_string(y) + "\r\n"; }
// The original Game Options panel's text: cream on green (its volume
// labels' font, 3452, sharp at its own size), a dark outline
std::string bodyText(int id, int x, int y, int dx, const std::string &dy = "fitfont", bool centred = false) {
  return section("Text" + std::to_string(id),
                 "type=UIText\r\nid=" + std::to_string(id) + "\r\n" + at(x, y) + "dx=" + std::to_string(dx) +
                     "\r\ndy=" + dy +
                     "\r\nfont=3452\r\nfontsize=3453\r\nforecolor=237\r\nforecolor=228\r\nforecolor=173\r\n"
                     "backcolor=100\r\nbackcolor=108\r\nbackcolor=108\r\nborder=1\r\ntransparent=1\r\n"
                     "layer=2\r\n" +
                     (centred ? "justify=center\r\n" : ""));
}
std::string goldButton(int id, int x, int y, int textId = 0) {
  return section("Button" + std::to_string(id),
                 "type=UIButton\r\nid=" + std::to_string(id) + "\r\n" + at(x, y) +
                     (textId ? "textid=" + std::to_string(textId) + "\r\n" : std::string()) +
                     "justify=center\r\nfont=14000\r\nfontsize=14001\r\nforecolor=255\r\nforecolor=186\r\n"
                     "forecolor=16\r\nbackcolor=131\r\nbackcolor=125\r\nbackcolor=53\r\nhighlightcolor=255\r\n"
                     "highlightcolor=223\r\nhighlightcolor=41\r\nselectcolor=90\r\nselectcolor=255\r\n"
                     "selectcolor=255\r\ngraycolor=132\r\ngraycolor=149\r\ngraycolor=124\r\n"
                     "animation=ui/gameopts/textbck/textbck\r\nlayer=2\r\n");
}
// A tick box: the game's own (ui/sharedui/checkbx, 22 x 18: a box, lit
// under the mouse, ticked when on) - on or off (state 2048), or one of a
// set's choices (4096)
std::string tickBox(int id, int x, int y, bool inSet) {
  return section("Tick" + std::to_string(id), "type=UIButton\r\nid=" + std::to_string(id) + "\r\n" + at(x, y) +
                                                  "animation=ui/sharedui/checkbx/checkbx\r\nlayer=2\r\ntransparent=1\r\n"
                                                  "state=" +
                                                  (inSet ? "4096" : "2048") + "\r\n");
}
std::string radioSet(int id, const std::vector<int> &buttons) {
  std::string b = "type=UIRadioSet\r\nid=" + std::to_string(id) + "\r\n";
  for (int i : buttons)
    b += "button=" + std::to_string(i) + "\r\n";
  return section("Set" + std::to_string(id), b);
}
std::string slider(int id, int x, int y, int dx = 110) {
  return section("Slider" + std::to_string(id), "type=UIScrollBar\r\nid=" + std::to_string(id) + "\r\n" + at(x, y) +
                                                    "animation=ui/sharedui/horzslid/horzslid\r\nthumb=ui/sharedui/horzthm/"
                                                    "horzthm\r\ndx=" + std::to_string(dx) +
                                                    "\r\ndy=26\r\nlayer=2\r\ntransparent=1\r\n");
}

// The window's rows: each 30 high from kRowsY. In a row a slider fills
// it, its text is 7 down (the font's 12 high, centred on the slider's 26)
// and a tick box 4 down beside its own text
const int kRow = 30, kTextDown = 7, kTickDown = 4, kTickText = 26;

std::string optionsLayout() {
  // (kW x kH: the Help window's frame, stretched from its edges, drawn with
  // the zoo behind its panes by UiOptions::draw - not an element)
  std::string t = section("LayoutInfo", "x=center\r\ny=center\r\ndx=" + std::to_string(kW) +
                                            "\r\ndy=" + std::to_string(kH) + "\r\nid=9099\r\nlayer=6\r\nstate=1");
  // (Game Options: the original panel's own title, string 1540)
  t += section("Title", "type=UIText\r\nid=" + std::to_string(kTitle) + "\r\ntextid=1540\r\nx=" +
                            std::to_string((kW - 217) / 2) +
                            "\r\ny=2\r\ndx=217\r\ndy=fitfont\r\nfont=3270\r\nfontsize=3271\r\n"
                            "border=2\r\nforecolor=255\r\nforecolor=228\r\nforecolor=173\r\nbackcolor=100\r\n"
                            "backcolor=108\r\nbackcolor=108\r\ntransparent=1\r\nshadowtext=0\r\nlayer=5\r\njustify=center");
  t += section("CloseWindow", "type=UIButton\r\nid=" + std::to_string(kClose) + "\r\nhelpid=3112\r\nx=" +
                                  std::to_string(kW - 47) + "\r\ny=1\r\nanimation=ui/sharedui/close/close\r\nlayer=3");
  t += section("PageTitle", "type=UIText\r\nid=" + std::to_string(kPageTitle) + "\r\nx=182\r\ny=29\r\ndx=" +
                                std::to_string(kW - 197) +
                                "\r\ndy=fitfont\r\nfont=25001\r\nfontsize=25002\r\n"
                                "forecolor=255\r\nforecolor=218\r\nforecolor=90\r\nbackcolor=59\r\nbackcolor=71\r\n"
                                "backcolor=45\r\nborder=1\r\ntransparent=1\r\nshadowtext=1\r\nlayer=2\r\njustify=center");
  // The pages, down the left
  std::vector<int> tabs;
  for (int p = 0, row = 0; p < kTabs; p++) {
    int id = kTabFirst + p;
    tabs.push_back(id);
    // (no zoo open: no Game page, the rest closed up)
    std::string b = goldButton(id, 34, 40 + (p == kGame && !g_makingInGame ? 0 : row++) * 34);
    t += b.substr(0, b.size() - 2) + "state=4096\r\n\r\n";
  }
  t += radioSet(kTabSet, tabs);
  t += goldButton(kDone, 34, kH - 65);
  // What the setting under the mouse does: three lines at the page's foot
  t += bodyText(kAbout, kLabelX, kH - 90, kW - 222, "62");

  auto label = [&t](int id, int y, int dx = 156) { t += bodyText(id, kLabelX, y + kTextDown, dx); };
  // A choice: its tick box and words (k, k + 1)
  auto choice = [&t](int id, int x, int y, int dx) {
    t += tickBox(id, x, y + kTickDown, true);
    t += bodyText(id + 1, x + kTickText, y + kTextDown, dx);
  };
  // An on/off: its tick box and words (k, k + 1)
  auto onOff = [&t](int id, int x, int y, int dx) {
    t += tickBox(id, x, y + kTickDown, false);
    t += bodyText(id + 1, x + kTickText, y + kTextDown, dx);
  };

  // Game: the original panel's four buttons (their own strings), in the
  // middle of the page - across, and down between its title and the words
  // at its foot (kAbout)
  {
    const int blockH = 3 * 38 + 24, top = kRowsY, bottom = kH - 96;
    const int y0 = top + (bottom - top - blockH) / 2;
    for (int i = 0; i < 4; i++)
      t += goldButton(pageId(kGame, kLoad + i), 182 + (kW - 194 - 116) / 2, y0 + i * 38, 1501 + i);
  }

  // Display
  int y = kRowsY;
  label(pageId(kDisplay, kModeLabel), y);
  // (three: started left of the controls' column, room for their words)
  choice(pageId(kDisplay, kModeWin), kControlX - 50, y, 60);
  choice(pageId(kDisplay, kModeBorder), kControlX + 40, y, 80);
  choice(pageId(kDisplay, kModeFull), kControlX + 150, y, 80);
  t += radioSet(pageId(kDisplay, kModeSet),
                {pageId(kDisplay, kModeWin), pageId(kDisplay, kModeBorder), pageId(kDisplay, kModeFull)});
  y += kRow + 4;
  label(pageId(kDisplay, kResLabel), y);
  t += slider(pageId(kDisplay, kResBar), kControlX, y, kControlW - 30);
  y += kRow;
  label(pageId(kDisplay, kFpsLimLabel), y);
  t += slider(pageId(kDisplay, kFpsLimBar), kControlX, y, kControlW - 30);
  y += kRow;
  onOff(pageId(kDisplay, kVsync), kLabelX, y, 380);
  y += kRow;
  label(pageId(kDisplay, kBrightLabel), y);
  t += slider(pageId(kDisplay, kBrightBar), kControlX, y, kControlW - 30);
  y += kRow;
  onOff(pageId(kDisplay, kFps), kLabelX, y, 380);

  // Graphics
  y = kRowsY;
  label(pageId(kGraphics, kScaleLabel), y);
  // (wide: every 0.05 a step of its own)
  t += slider(pageId(kGraphics, kScaleBar), kControlX, y, kControlW - 10);
  t += tickBox(pageId(kGraphics, kScaleAuto), kControlX, y + 28, false);
  t += bodyText(pageId(kGraphics, kScaleNote), kControlX + kTickText, y + 31, 200);
  y += kRow + 26;
  label(pageId(kGraphics, kArtLabel), y);
  // (five: two rows - the graphics card's two, then the plain looks)
  choice(pageId(kGraphics, kArtFsr), kControlX, y, 50);
  choice(pageId(kGraphics, kArtStyled), kControlX + 86, y, 70);
  choice(pageId(kGraphics, kArtSharp), kControlX, y + 24, 50);
  choice(pageId(kGraphics, kArtSmooth), kControlX + 86, y + 24, 56);
  choice(pageId(kGraphics, kArtMmpx), kControlX + 172, y + 24, 50);
  t += radioSet(pageId(kGraphics, kArtSet),
                {pageId(kGraphics, kArtFsr), pageId(kGraphics, kArtSharp), pageId(kGraphics, kArtSmooth),
                 pageId(kGraphics, kArtMmpx), pageId(kGraphics, kArtStyled)});
  y += kRow + 24;
  // (what the art looks like now: three lines)
  t += bodyText(pageId(kGraphics, kArtNote), kLabelX, y + 2, kW - 222, "62");
  y += 66;
  label(pageId(kGraphics, kSharpLabel), y);
  t += slider(pageId(kGraphics, kSharpBar), kControlX, y, kControlW - 30);

  // Sound
  y = kRowsY;
  label(pageId(kSound, kVolumeLabel), y);
  t += slider(pageId(kSound, kVolumeBar), kControlX, y, kControlW - 30);
  y += kRow;
  label(pageId(kSound, kMusicLabel), y);
  t += slider(pageId(kSound, kMusicBar), kControlX, y, kControlW - 30);

  // Controls
  y = kRowsY;
  label(pageId(kControls, kScrollLabel), y);
  t += slider(pageId(kControls, kScrollBar), kControlX, y, kControlW - 30);
  y += kRow;
  onOff(pageId(kControls, kEdge), kLabelX, y, 380);
  y += kRow;
  label(pageId(kControls, kTipsLabel), y);
  choice(pageId(kControls, kTipsShort), kControlX, y, 50);
  choice(pageId(kControls, kTipsLong), kControlX + 90, y, 50);
  t += radioSet(pageId(kControls, kTipsSet), {pageId(kControls, kTipsShort), pageId(kControls, kTipsLong)});
  y += kRow;
  label(pageId(kControls, kDelayLabel), y);
  t += slider(pageId(kControls, kDelayBar), kControlX, y, kControlW - 30);
  y += kRow;
  label(pageId(kControls, kDurationLabel), y);
  t += slider(pageId(kControls, kDurationBar), kControlX, y, kControlW - 30);
  y += kRow;
  // Which Game Options the disk button and Esc open: this window, or the
  // original's side panel
  label(pageId(kControls, kStyleLabel), y);
  choice(pageId(kControls, kStyleNew), kControlX, y, 86);
  choice(pageId(kControls, kStyleOld), kControlX + 116, y, 110);
  t += radioSet(pageId(kControls, kStyleSet), {pageId(kControls, kStyleNew), pageId(kControls, kStyleOld)});
  y += kRow;
  // Where the windows go: the ones dragged put back
  label(pageId(kControls, kWinLabel), y);
  t += goldButton(pageId(kControls, kWinReset), kControlX, y - 2);

  // Accessibility
  y = kRowsY;
  label(pageId(kAccess, kFilterLabel), y);
  choice(pageId(kAccess, kFilterOff), kControlX, y, 90);
  choice(pageId(kAccess, kFilterP), kControlX + 124, y, 100);
  choice(pageId(kAccess, kFilterD), kControlX, y + 24, 100);
  choice(pageId(kAccess, kFilterT), kControlX + 124, y + 24, 100);
  t += radioSet(pageId(kAccess, kFilterSet), {pageId(kAccess, kFilterOff), pageId(kAccess, kFilterP),
                                              pageId(kAccess, kFilterD), pageId(kAccess, kFilterT)});
  y += kRow + 26;
  label(pageId(kAccess, kStrengthLabel), y);
  t += slider(pageId(kAccess, kStrengthBar), kControlX, y, kControlW - 30);
  y += kRow + 6;
  label(pageId(kAccess, kCursorLabel), y);
  choice(pageId(kAccess, kCursor1), kControlX, y, 50);
  choice(pageId(kAccess, kCursor2), kControlX + 80, y, 50);
  choice(pageId(kAccess, kCursor3), kControlX + 160, y, 50);
  t += radioSet(pageId(kAccess, kCursorSet),
                {pageId(kAccess, kCursor1), pageId(kAccess, kCursor2), pageId(kAccess, kCursor3)});

  // Shaders: the shaders, each on or off with its Settings button; under
  // them the shown one's settings, three sliders across
  for (int i = 0; i < kShaderRows; i++) {
    int id = pageId(kShaders, kShaderFirst + 3 * i), ry = kRowsY + i * 34;
    t += tickBox(id, kLabelX, ry + kTickDown + 2, false);
    t += bodyText(id + 1, kLabelX + kTickText, ry + kTextDown + 2, 270);
    t += goldButton(id + 2, kW - 146, ry);
  }
  t += bodyText(pageId(kShaders, kShadersNone), kLabelX, kRowsY + 4, kW - 222, "52");
  for (int j = 0; j < kSettingRows; j++) {
    int id = pageId(kShaders, kSettingFirst + 2 * j);
    int sx = kLabelX + (j % 3) * 144, sy = kRowsY + kShaderRows * 34 + 6 + (j / 3) * 42;
    t += bodyText(id, sx, sy, 140);
    t += slider(id + 1, sx, sy + 13, 132);
  }

  // Mods & Updates: the Mod Menu (it was Get New Zoo Tycoon Items' on the
  // main menu) and what it's for; the switches; then the mods themselves,
  // each on or off right here
  t += goldButton(pageId(kMods, kModMenu), kLabelX, kRowsY + 2);
  t += bodyText(pageId(kMods, kModMenuText), kLabelX + 126, kRowsY + 4, kW - 348, "26");
  t += bodyText(pageId(kMods, kModsTitle), kLabelX, kRowsY + 118, 132);
  t += bodyText(pageId(kMods, kModsNote), kLabelX + 150, kRowsY + 118, kW - 372);
  const int modRowsY = kRowsY + 134, modRowStep = 24;
  for (int i = 0; i < kModRows; i++) {
    onOff(pageId(kMods, kModFirst + 2 * i), kLabelX, modRowsY + i * modRowStep, 270);
    t += goldButton(pageId(kMods, kModSettingsFirst + i), kW - 146, modRowsY + i * modRowStep);
  }

  // Compact arrows and the page number fit between the list and its footer.
  const int modArrowW = 32, modArrowH = 24, modPagerGap = 8, modPageTextW = 96;
  const int modPagerW = 2 * modArrowW + 2 * modPagerGap + modPageTextW;
  const int modPagerX = kLabelX + (kW - 222 - modPagerW) / 2;
  const int modPagerY = modRowsY + kModRows * modRowStep + 8;
  auto modPageButton = [&t](int id, int x, int y) {
    std::string b = goldButton(id, x, y);
    t += b.substr(0, b.size() - 2) + "dx=" + std::to_string(modArrowW) +
         "\r\ndy=" + std::to_string(modArrowH) + "\r\n\r\n";
  };
  modPageButton(pageId(kMods, kModPrev), modPagerX, modPagerY);
  t += bodyText(pageId(kMods, kModPageText), modPagerX + modArrowW + modPagerGap,
                modPagerY + kTextDown, modPageTextW, "fitfont", true);
  modPageButton(pageId(kMods, kModNext), modPagerX + modPagerW - modArrowW, modPagerY);

  // A mod's settings: Back and its name, then its settings three across - a
  // slider under its words, or a tick box beside them
  t += goldButton(pageId(kModSet, kMsBack), kLabelX, kRowsY + 2);
  t += bodyText(pageId(kModSet, kMsName), kLabelX + 126, kRowsY + 4, kW - 348, "26");
  t += bodyText(pageId(kModSet, kMsNone), kLabelX, kRowsY + 44, kW - 222, "52");
  for (int j = 0; j < kMsRows; j++) {
    int id = pageId(kModSet, kMsFirst + 4 * j);
    int sx = kLabelX + (j % 3) * 144, sy = kRowsY + 44 + (j / 3) * 42;
    t += bodyText(id, sx, sy, 140);
    t += slider(id + 1, sx, sy + 13, 132);
    t += tickBox(id + 2, sx, sy + 12, false);
    t += bodyText(id + 3, sx + kTickText, sy + 15, 114);
  }

  // The extras, each on its page, below that page's other rows (24 apart)
  int next[kPages] = {};
  next[kControls] = kRowsY + 7 * kRow + 6;
  next[kGameplay] = kRowsY;
  next[kMods] = kRowsY + 40;
  int count[kPages] = {};
  for (const Features::Switch &f : Features::all()) {
    int p = extraPage(f.name);
    if (p < 0)
      continue;
    int k = count[p]++;
    int ry = next[p];
    next[p] += 24;
    t += tickBox(pageId(p, kSwitchFirst + k), kLabelX, ry + kTickDown, false);
    t += bodyText(pageId(p, kSwitchTextFirst + k), kLabelX + kTickText, ry + kTextDown, 380);
    // (Check for updates: Check now - or Install now - at its end)
    if (std::string(f.name) == "updatechecks")
      t += goldButton(pageId(p, kUpdateNow), kW - 146, ry - 2);
  }
  return t;
}

int updatesRow() {
  int k = 0;
  for (const Features::Switch &f : Features::all()) {
    if (extraPage(f.name) != kMods)
      continue;
    if (std::string(f.name) == "updatechecks")
      return k;
    k++;
  }
  return -1;
}
} // namespace

// ============================================================================
// The window
// ============================================================================
UiOptions::UiOptions(ResourceManager *rm) : rm(rm) {
  rm->provide("ui/zt1options.lyt", [](ResourceManager *) { return optionsLayout(); });
}

UiOptions::~UiOptions() { delete this->layout; }

void UiOptions::open(bool inGame, int page) {
  if (this->layout)
    return;
  g_makingInGame = inGame;
  IniReader *ini = this->rm->getIniReader("ui/zt1options.lyt");
  if (!ini)
    return;
  this->layout = new UiLayout(ini, this->rm);
  this->inGame = inGame;
  this->closeAsked = false;
  this->justOpened = true;
  this->pending = UiAction::NONE;
  this->about.clear();
  this->wire();
  this->setPage(page >= 0 ? page : inGame ? kGame : kDisplay);
}

void UiOptions::hideRow(std::initializer_list<int> ids, bool hide) {
  for (int id : ids) {
    if (hide)
      this->hiddenIds.insert(id);
    else
      this->hiddenIds.erase(id);
    if (UiElement *e = this->layout ? this->layout->getElementById(id) : nullptr)
      e->setHidden(hide || pageOf(id) != this->page);
  }
}

// Display's Resolution row for the window as it is now: the sizes to pick
// from (windowed, the window's; full screen, the monitor's modes), or -
// borderless, or in the Lab - just the size, nothing to drag
void UiOptions::showResolutions() {
  UiText *label = dynamic_cast<UiText *>(this->layout->getElementById(pageId(kDisplay, kResLabel)));
  UiScrollBar *s = dynamic_cast<UiScrollBar *>(this->layout->getElementById(pageId(kDisplay, kResBar)));
  this->listedMode = static_cast<int>(Display::mode());
  this->resolutionPending = -1;
  this->resolutions.clear();
  for (const Display::Size &r : Display::resolutions())
    this->resolutions.insert(this->resolutions.begin(), SDL_Point{r.w, r.h}); // (smallest first)
  auto name = [](int w, int h) { return "Resolution: " + std::to_string(w) + " x " + std::to_string(h); };
  Display::Size now = Display::resolution();
  bool pickable = Display::resolutionPickable() && this->resolutions.size() > 1;
  if (label)
    label->setText(name(now.w, now.h) + (Display::mode() == Display::Mode::Borderless ? " (the monitor's)" : ""));
  this->hideRow({pageId(kDisplay, kResBar)}, !pickable);
  if (!s || !pickable)
    return;
  const int n = static_cast<int>(this->resolutions.size());
  int at = 0;
  for (int i = 0; i < n; i++)
    if (this->resolutions[i].x == now.w && this->resolutions[i].y == now.h)
      at = i;
  s->setValue(at * 100 / (n - 1));
  s->onChange = [this, label, name, n](int v) {
    int i = std::clamp((v * (n - 1) + 50) / 100, 0, n - 1);
    this->resolutionPending = i;
    if (label)
      label->setText(name(this->resolutions[i].x, this->resolutions[i].y));
  };
}

// Mods & Updates' list: each mod in the mods folder, on or off (the first
// few; the Mod Menu has them all), and what a change needs
void UiOptions::showMods() {
  UiLayout *L = this->layout;
  auto text = [L](int id, const std::string &s) {
    if (UiText *t = dynamic_cast<UiText *>(L->getElementById(id)))
      t->setText(s);
  };
  std::vector<Mods::Mod> mods = Mods::list();
  const int total = static_cast<int>(mods.size());
  modStart = std::clamp(modStart, 0, total ? ((total - 1) / kModRows) * kModRows : 0);
  if (UiButton *b = dynamic_cast<UiButton *>(L->getElementById(pageId(kMods, kModPrev)))) {
    b->setLabel("<");
    b->setDisabled(modStart == 0);
    b->onClick = [this] { modStart = std::max(0, modStart - kModRows); showMods(); };
  }
  if (UiButton *b = dynamic_cast<UiButton *>(L->getElementById(pageId(kMods, kModNext)))) {
    b->setLabel(">");
    b->setDisabled(modStart + kModRows >= total);
    b->onClick = [this] { modStart += kModRows; showMods(); };
  }
  this->about[pageId(kMods, kModPrev)] = "Previous page.";
  this->about[pageId(kMods, kModNext)] = "Next page.";
  const int pageCount = total ? (total - 1) / kModRows + 1 : 0;
  text(pageId(kMods, kModPageText),
       total ? "Page " + std::to_string(modStart / kModRows + 1) + " of " + std::to_string(pageCount) : "0 mods");
  text(pageId(kMods, kModsTitle), mods.empty() ? "Your mods: none yet" : "Your mods (" + std::to_string(mods.size()) + ")");
  std::string note;
  if (!Features::mods)
    note = "Load mods is off: none load.";
  else if (!Mods::lastError().empty())
    note = "Required content stays on.";
  else if (ModMenu::changed())
    note = "Applying changes...";
  else if (!mods.empty())
    note = "Changes apply in this session.";
  else if (mods.empty())
    note = "Add some to the mods folder (Mod Menu).";
  text(pageId(kMods, kModsNote), note);
  for (int i = 0; i < kModRows; i++) {
    int box = pageId(kMods, kModFirst + 2 * i), words = box + 1, more = pageId(kMods, kModSettingsFirst + i);
    bool shown = modStart + i < total;
    this->hideRow({box, words}, !shown);
    this->hideRow({more}, !shown || mods[modStart + i].settings.empty());
    if (!shown)
      continue;
    const Mods::Mod &m = mods[modStart + i];
    const std::string required = m.enabled ? Mods::offReason(m.folder) : "";
    // (its own settings: a page of their own)
    if (UiButton *b = dynamic_cast<UiButton *>(L->getElementById(more))) {
      b->setLabel("Settings");
      std::string folder = m.folder;
      b->onClick = [this, folder] {
        this->modShown = folder;
        this->showModSettings();
        this->setPage(kModSet);
      };
    }
    this->about[more] = "This mod's own settings.";
    text(words, m.name + (required.empty() ? "" : " [Required]"));
    this->about[box] = this->about[words] = required.empty()
        ? aboutText(m.about.empty() ? "A mod in " + m.folder + "." : m.about)
        : required;
    if (UiButton *b = dynamic_cast<UiButton *>(L->getElementById(box))) {
      b->setToggledOn(m.enabled);
      b->setDisabled(!required.empty());
      std::string folder = m.folder;
      b->onClick = [this, b, folder] {
        Mods::setEnabled(folder, b->isToggledOn());
        HdArt::refresh(); // (an HD art mod, a shader mod: on or off at once)
        Shaders::refresh();
        this->showMods();
        this->showShaders();
      };
    }
  }
}

// A mod's settings page (modShown): Back, its name, and each of its settings
// (the first twelve) - a slider saying its value (a choice's, the choice),
// or a tick box - at work and kept the moment it's changed
void UiOptions::showModSettings() {
  UiLayout *L = this->layout;
  auto text = [L](int id, const std::string &s) {
    if (UiText *t = dynamic_cast<UiText *>(L->getElementById(id)))
      if (t->getText() != s)
        t->setText(s);
  };
  Mods::Mod mod;
  for (const Mods::Mod &m : Mods::list())
    if (m.folder == this->modShown)
      mod = m;
  if (UiButton *b = dynamic_cast<UiButton *>(L->getElementById(pageId(kModSet, kMsBack)))) {
    b->setLabel("Back");
    b->onClick = [this] {
      this->showMods();
      this->setPage(kMods);
    };
  }
  this->about[pageId(kModSet, kMsBack)] = "Back to Mods & Updates.";
  text(pageId(kModSet, kMsName), mod.name + (mod.enabled ? "" : "  (switched off)"));
  this->about[pageId(kModSet, kMsName)] = aboutText(mod.about.empty() ? "A mod in " + mod.folder + "." : mod.about);
  text(pageId(kModSet, kMsNone), mod.settings.empty() ? "This mod has no settings." : "");
  this->hideRow({pageId(kModSet, kMsNone)}, !mod.settings.empty());
  for (int j = 0; j < kMsRows; j++) {
    int id = pageId(kModSet, kMsFirst + 4 * j);
    const bool has = j < static_cast<int>(mod.settings.size());
    const bool toggle = has && mod.settings[j].type == "toggle";
    this->hideRow({id, id + 1}, !has || toggle);
    this->hideRow({id + 2, id + 3}, !toggle);
    if (!has)
      continue;
    const Mods::Setting &t = mod.settings[j];
    const std::string folder = mod.folder, name = t.name;
    const std::string about = aboutText(t.about.empty() ? t.label : t.about);
    for (int k = 0; k < 4; k++)
      this->about[id + k] = about;
    if (toggle) {
      text(id + 3, t.label);
      if (UiButton *b = dynamic_cast<UiButton *>(L->getElementById(id + 2))) {
        b->setToggledOn(t.value != 0);
        b->onClick = [b, folder, name] {
          float v = b->isToggledOn() ? 1.0f : 0.0f;
          Mods::setSetting(folder, name, v);
          Shaders::setModValue(folder, name, v);
        };
      }
      continue;
    }
    auto label = [](const Mods::Setting &v, float value) {
      if (v.type == "choice") {
        int c = std::clamp(static_cast<int>(std::lround(value)), 0, static_cast<int>(v.choices.size()) - 1);
        return v.label + ": " + v.choices[c];
      }
      char b[96];
      std::snprintf(b, sizeof b, std::fabs(v.max - v.min) > 10 ? "%s: %.0f" : "%s: %.2f", v.label.c_str(), value);
      return std::string(b);
    };
    text(id, label(t, t.value));
    if (UiScrollBar *s = dynamic_cast<UiScrollBar *>(L->getElementById(id + 1))) {
      const float range = t.max - t.min;
      s->setValue(std::fabs(range) < 1e-6f
                      ? 0
                      : std::clamp(static_cast<int>(std::lround((t.value - t.min) / range * 100.0f)), 0, 100));
      Mods::Setting copy = t;
      s->onChange = [text, id, folder, name, copy, range, label](int v) {
        float value = copy.min + v / 100.0f * range;
        // (a choice: one of them)
        if (copy.type == "choice")
          value = std::round(value);
        Mods::setSetting(folder, name, value);
        Shaders::setModValue(folder, name, value);
        text(id, label(copy, value));
      };
    }
  }
}

// The Shaders page: the shader mods switched on (the first three) - each on
// or off, its Settings button - and the shown one's settings as sliders,
// each saying its value
void UiOptions::showShaders() {
  UiLayout *L = this->layout;
  auto text = [L](int id, const std::string &s) {
    if (UiText *t = dynamic_cast<UiText *>(L->getElementById(id)))
      if (t->getText() != s)
        t->setText(s);
  };
  std::vector<Shaders::Shader> &list = Shaders::all();
  const int n = std::min(static_cast<int>(list.size()), static_cast<int>(kShaderRows));
  this->shaderShown = std::clamp(this->shaderShown, 0, std::max(0, n - 1));
  text(pageId(kShaders, kShadersNone),
       n ? "" : aboutText("No shader mods are on. A mod with a shaders folder brings them: switch it on in Mods & Updates."));
  this->hideRow({pageId(kShaders, kShadersNone)}, n > 0);
  for (int i = 0; i < kShaderRows; i++) {
    int id = pageId(kShaders, kShaderFirst + 3 * i);
    this->hideRow({id, id + 1, id + 2}, i >= n);
    if (i >= n)
      continue;
    const Shaders::Shader &s = list[i];
    std::string key = s.key;
    text(id + 1, s.name + (s.failed ? "  (didn't compile)" : "") + (i == this->shaderShown && n > 1 ? "  <" : ""));
    this->about[id] = this->about[id + 1] = aboutText(s.about.empty() ? s.name + ", from " + s.mod + "." : s.about);
    if (UiButton *b = dynamic_cast<UiButton *>(L->getElementById(id))) {
      b->setToggledOn(s.on);
      b->onClick = [b, key] { Shaders::setOn(key, b->isToggledOn()); };
    }
    if (UiButton *b = dynamic_cast<UiButton *>(L->getElementById(id + 2))) {
      b->setLabel("Settings");
      b->onClick = [this, i] {
        this->shaderShown = i;
        this->showShaders();
      };
    }
    this->about[id + 2] = "Show this shader's settings below.";
  }
  const Shaders::Shader *shown = n ? &list[this->shaderShown] : nullptr;
  for (int j = 0; j < kSettingRows; j++) {
    int id = pageId(kShaders, kSettingFirst + 2 * j);
    const bool has = shown && j < static_cast<int>(shown->settings.size());
    this->hideRow({id, id + 1}, !has);
    if (!has)
      continue;
    const Shaders::Setting &t = shown->settings[j];
    auto label = [](const Shaders::Setting &v, float value) {
      char b[96];
      // (a wide range - an angle - in whole numbers)
      std::snprintf(b, sizeof b, v.max - v.min > 10 ? "%s: %.0f" : "%s: %.2f", v.label.c_str(), value);
      return std::string(b);
    };
    text(id, label(t, t.value));
    this->about[id] = this->about[id + 1] = aboutText(t.about.empty() ? t.label : t.about);
    if (UiScrollBar *s = dynamic_cast<UiScrollBar *>(L->getElementById(id + 1))) {
      const float range = std::max(1e-6f, t.max - t.min);
      s->setValue(std::clamp(static_cast<int>(std::lround((t.value - t.min) / range * 100.0f)), 0, 100));
      std::string key = shown->key, name = t.name;
      Shaders::Setting copy = t;
      s->onChange = [text, id, key, name, copy, range, label](int v) {
        float value = copy.min + v / 100.0f * range;
        Shaders::setValue(key, name, value);
        text(id, label(copy, value));
      };
    }
  }
}

// Mods & Updates' Check for updates row: what the updater is doing, and the
// button for it
void UiOptions::showUpdates() {
  int row = updatesRow();
  if (row < 0 || !this->layout)
    return;
  std::string why;
  bool can = Updater::possible(&why);
  Updater::State st = Updater::state();
  std::string said = can ? Updater::status() : why;
  if (UiText *t = dynamic_cast<UiText *>(this->layout->getElementById(pageId(kMods, kSwitchTextFirst + row))))
    t->setText(said.empty() ? std::string("Check for updates") : "Check for updates: " + said);
  this->about[pageId(kMods, kSwitchTextFirst + row)] = this->about[pageId(kMods, kSwitchFirst + row)] =
      can ? "An installed copy looks for a new version 15 s after it starts and every 30 minutes. One found is "
            "downloaded and its signature checked, then you're asked at the main menu (Later: installed when you "
            "close the game)."
          : "The Game Build zip, a checkout and the Lab's Play don't update themselves: get the new version from the "
            "releases, or install the game with its setup.exe.";
  this->hideRow({pageId(kMods, kUpdateNow)}, !can);
  if (UiButton *b = dynamic_cast<UiButton *>(this->layout->getElementById(pageId(kMods, kUpdateNow)))) {
    b->setLabel(st == Updater::State::Ready ? "Install now" : "Check now");
    b->setDisabled(st == Updater::State::Checking || st == Updater::State::Downloading);
  }
}

void UiOptions::tick(SDL_Renderer *renderer) {
  UiLayout *L = this->layout;
  if (this->page == kMods)
    this->showUpdates();
  // Resolution: the size dragged to, used once it's let go; and the list
  // again after the window mode changed (Alt+Enter too)
  // (the mode at work ticked - after Alt+Enter too, and in the Lab's Play,
  // where the Lab owns the window and a click changes nothing)
  if (UiButton *b = dynamic_cast<UiButton *>(L->getElementById(pageId(kDisplay, kModeWin + 2 * static_cast<int>(Display::mode())))))
    if (!b->isToggledOn())
      b->choose();
  if (static_cast<int>(Display::mode()) != this->listedMode)
    this->showResolutions();
  if (UiScrollBar *s = dynamic_cast<UiScrollBar *>(L->getElementById(pageId(kDisplay, kResBar))))
    if (this->resolutionPending >= 0 && !s->isDragging()) {
      SDL_Point p = this->resolutions[this->resolutionPending];
      this->resolutionPending = -1;
      Display::setResolution({p.x, p.y});
      this->showResolutions();
    }
  // Rows that only apply sometimes
  this->hideRow({pageId(kGraphics, kSharpLabel), pageId(kGraphics, kSharpBar)},
                !((Display::art() == Display::Art::Fsr || Display::art() == Display::Art::Stylized) &&
                  GpuFsr::available()));
  this->hideRow({pageId(kAccess, kStrengthLabel), pageId(kAccess, kStrengthBar)},
                Display::filter() == Display::Filter::Off);
  // What the art looks like now
  if (this->page == kGraphics) {
    int w = 800, h = 600;
    if (SDL_Window *win = SDL_RenderGetWindow(renderer))
      SDL_GetWindowSizeInPixels(win, &w, &h);
    float ui = RenderSettings::uiScaleHeld > 0.0f ? RenderSettings::uiScaleHeld : RenderSettings::uiScaleFor(w, h);
    bool mapBigger = this->inGame && g_ZoomLevel > 1.001f;
    Display::Art a = Display::art(), started = Display::artAtStart();
    const char *look = a == Display::Art::Fsr        ? "upscaled by FSR on your graphics card"
                       : a == Display::Art::Stylized ? "upscaled by FSR with dark outlines and livelier colours"
                       : a == Display::Art::Sharp    ? "drawn as crisp square pixels"
                       : a == Display::Art::Smooth ? "blended smoothly as it's stretched"
                                                   : "upscaled by MMPX";
    std::string note;
    if (a == Display::Art::Mmpx && started != Display::Art::Mmpx)
      note = "MMPX is made as the art loads: you'll see it the next time the game starts.";
    else if (started == Display::Art::Mmpx && a != Display::Art::Mmpx)
      note = "The game started with MMPX: this change shows the next time it starts.";
    else if (HdArt::active() && mapBigger)
      note = "An HD art mod is on: zoomed in, the map's art is its own redrawn art (the menus as picked here).";
    else if ((a == Display::Art::Fsr || a == Display::Art::Stylized) && !GpuFsr::available())
      note = "FSR on the graphics card needs OpenGL, which isn't available here: the art was upscaled as it loaded.";
    else if (ui <= 1.001f && !mapBigger)
      note = std::string("Nothing is drawn bigger right now (Interface Size 1x") + (this->inGame ? ", the map at 1x" : "") +
             "), so every choice looks the same. Raise Interface Size" + (this->inGame ? " or zoom in" : "") +
             " to see the difference.";
    else
      note = std::string("Drawn bigger") + (ui > 1.001f ? " (Interface Size " + scaleName(ui) + ")" : "") +
             (mapBigger ? std::string(ui > 1.001f ? " and " : " (") + "the map zoomed in" + (ui > 1.001f ? "" : ")") : "") +
             ", the art is " + look + ".";
    if (UiText *t = dynamic_cast<UiText *>(L->getElementById(pageId(kGraphics, kArtNote)))) {
      std::string wrapped = aboutText(note);
      if (t->getText() != wrapped)
        t->setText(wrapped);
    }
  }
}

void UiOptions::finishClose() {
  delete this->layout;
  this->layout = nullptr;
  this->hiddenIds.clear();
  this->scale.attach(nullptr, nullptr, nullptr, nullptr, "", true);
  RenderSettings::uiScaleHeld = 0.0f; // (closed mid-drag)
  this->closeAsked = false;
}

void UiOptions::setPage(int page) {
  if (!this->inGame && page == kGame)
    page = kDisplay;
  this->page = page;
  // Each page's own elements shown, the others' hidden
  for (int p = 0; p < kPages; p++)
    for (int k = 1; k < 100; k++)
      if (UiElement *e = this->layout->getElementById(pageId(p, k)))
        e->setHidden(p != page || this->hiddenIds.count(pageId(p, k)));
  // (a mod's settings: under Mods & Updates' tab, titled with the mod)
  if (UiButton *tab = dynamic_cast<UiButton *>(this->layout->getElementById(kTabFirst + (page == kModSet ? kMods : page))))
    tab->choose();
  if (UiText *title = dynamic_cast<UiText *>(this->layout->getElementById(kPageTitle))) {
    std::string name = kPageNames[page];
    if (page == kModSet)
      for (const Mods::Mod &m : Mods::list())
        if (m.folder == this->modShown)
          name = m.name + " settings";
    title->setText(name);
  }
  if (UiText *a = dynamic_cast<UiText *>(this->layout->getElementById(kAbout)))
    a->setText(aboutText(kPageAbout[page]));
}

void UiOptions::wire() {
  UiLayout *L = this->layout;
  auto text = [L](int id, const std::string &s) {
    if (UiText *t = dynamic_cast<UiText *>(L->getElementById(id)))
      t->setText(s);
  };
  auto button = [L](int id) { return dynamic_cast<UiButton *>(L->getElementById(id)); };
  auto bar = [L](int id) { return dynamic_cast<UiScrollBar *>(L->getElementById(id)); };
  auto describe = [this](std::initializer_list<int> ids, const std::string &s) {
    for (int id : ids)
      this->about[id] = s;
  };

  // The frame
  for (int p = 0; p < kTabs; p++)
    if (UiButton *b = button(kTabFirst + p)) {
      b->setLabel(kPageNames[p]);
      b->setHidden(p == kGame && !this->inGame); // (no zoo: nothing to save)
      b->onClick = [this, p] { this->setPage(p); };
    }
  if (UiButton *b = button(kDone)) {
    b->setLabel("Done");
    b->onClick = [this] { this->close(); };
  }
  if (UiButton *b = button(kClose))
    b->onClick = [this] { this->close(); };

  // A set of choices: each tick box's words, which is ticked, and what a
  // click does (with its index)
  auto choices = [&](int page, std::initializer_list<int> ks, std::initializer_list<const char *> words, int now,
                     std::function<void(int)> picked) {
    int i = 0;
    auto w = words.begin();
    for (int k : ks) {
      text(pageId(page, k + 1), *w++);
      if (UiButton *b = button(pageId(page, k))) {
        if (i == now)
          b->choose();
        b->onClick = [picked, i] { picked(i); };
      }
      i++;
    }
  };
  // An on/off tick box and its words
  auto onOff = [&](int page, int k, const char *words, bool now, std::function<void(bool)> set) {
    text(pageId(page, k + 1), words);
    if (UiButton *b = button(pageId(page, k))) {
      b->setToggledOn(now);
      b->onClick = [b, set] { set(b->isToggledOn()); };
    }
  };

  // Mods & Updates: the mods, each on or off here (they load when the game
  // next starts), and the Mod Menu for each one's details and art (a screen
  // of the main menu's, coming back here: greyed in a zoo)
  if (UiButton *b = button(pageId(kMods, kModMenu))) {
    b->setLabel("Mod Menu");
    b->setDisabled(this->inGame || !Features::modMenu);
    b->onClick = [this] {
      if (this->inGame || !Features::modMenu)
        return;
      this->pending = UiAction::STARTUP_ZOO_ITEMS;
      this->close();
    };
  }
  text(pageId(kMods, kModMenuText), this->inGame ? "Each mod's details and pictures (from the main menu)."
                                                 : "Each mod's details and pictures, and its folder.");
  describe({pageId(kMods, kModMenu), pageId(kMods, kModMenuText)},
           this->inGame ? "The Mod Menu opens from the main menu's Game Options."
                        : "Each mod's name, author, what it changes and pictures of it; a different mods folder.");
  this->showMods();
  this->showShaders();
  // Check now: looks for an update at once (even with Check for updates
  // off); with one downloaded and checked, Install now (the game closes, the
  // installer runs and starts it again)
  if (UiButton *b = button(pageId(kMods, kUpdateNow))) {
    b->onClick = [this] {
      if (Updater::state() == Updater::State::Ready) {
        std::string why;
        if (!Updater::installNow(why))
          SDL_Log("[Updater] %s", why.c_str());
      } else {
        Updater::checkNow();
      }
    };
    this->about[pageId(kMods, kUpdateNow)] =
        "Looks for a new version now. One found is downloaded and checked first; then Install now closes the game, "
        "updates it and starts it again.";
  }
  this->showUpdates();

  // Game
  for (int i = 0; i < 4; i++)
    if (UiButton *b = button(pageId(kGame, kLoad + i))) {
      UiAction a = static_cast<UiAction>(static_cast<int>(UiAction::GAME_LOAD) + i);
      b->onClick = [this, a] {
        this->pending = a;
        this->close();
      };
    }

  // Display: the window (in the Lab's Play the Lab owns it: greyed there)
  text(pageId(kDisplay, kModeLabel), "Window");
  choices(kDisplay, {kModeWin, kModeBorder, kModeFull}, {"Window", "Borderless", "Full screen"},
          static_cast<int>(Display::mode()), [](int i) { Display::setMode(static_cast<Display::Mode>(i)); });
  describe({pageId(kDisplay, kModeLabel), pageId(kDisplay, kModeWin), pageId(kDisplay, kModeWin + 1)},
           Embed::active() ? "In the Lab's Play the Lab owns the window."
                           : "A window you can resize and move. Alt+Enter or F11 switch too (Extra hotkeys).");
  describe({pageId(kDisplay, kModeBorder), pageId(kDisplay, kModeBorder + 1)},
           "The whole screen at your monitor's own resolution, without a frame: quick to Alt+Tab out of.");
  describe({pageId(kDisplay, kModeFull), pageId(kDisplay, kModeFull + 1)},
           "The whole screen at the resolution picked below (the monitor switches to it).");
  // Resolution: the window's size, or full screen's (borderless: the
  // monitor's own). Shown as it's dragged, used when it's let go
  describe({pageId(kDisplay, kResLabel), pageId(kDisplay, kResBar)},
           "The window's size, or full screen's resolution. Borderless always uses the monitor's own.");
  this->showResolutions();
  // Frame rate limit and vsync
  {
    static const int kLimits[] = {30, 60, 75, 120, 144, 165, 240, 0};
    const int n = sizeof(kLimits) / sizeof(kLimits[0]);
    auto name = [](int fps) { return fps > 0 ? "Frame Rate Limit: " + std::to_string(fps) : std::string("Frame Rate Limit: None"); };
    int at = n - 1;
    for (int i = 0; i < n; i++)
      if (kLimits[i] == Display::frameLimit())
        at = i;
    text(pageId(kDisplay, kFpsLimLabel), name(kLimits[at]));
    if (UiScrollBar *s = bar(pageId(kDisplay, kFpsLimBar))) {
      s->setValue(at * 100 / (n - 1));
      s->onChange = [text, name, n](int v) {
        int i = std::clamp((v * (n - 1) + 50) / 100, 0, n - 1);
        if (kLimits[i] != Display::frameLimit())
          Display::setFrameLimit(kLimits[i]);
        text(pageId(kDisplay, kFpsLimLabel), name(kLimits[i]));
      };
    }
    describe({pageId(kDisplay, kFpsLimLabel), pageId(kDisplay, kFpsLimBar)},
             "The most frames a second the game draws. Lower saves power; vsync also holds it to the monitor's.");
  }
  onOff(kDisplay, kVsync, "Vertical sync: frames wait for the monitor (no tearing)", Display::vsync(),
        [](bool on) { Display::setVsync(on); });
  describe({pageId(kDisplay, kVsync), pageId(kDisplay, kVsync + 1)},
           "On, each frame waits for the monitor's refresh: no tearing lines. Off, frames show as soon as they're drawn.");
  // Brightness: a gamma, 0.5 to 2 (the slider's middle as drawn)
  {
    auto name = [](float b) { return "Brightness: " + std::to_string(static_cast<int>(std::lround(b * 100))) + "%"; };
    text(pageId(kDisplay, kBrightLabel), name(Display::brightness()));
    if (UiScrollBar *s = bar(pageId(kDisplay, kBrightBar))) {
      float b = Display::brightness();
      s->setValue(static_cast<int>(std::lround(b <= 1.0f ? (b - 0.5f) * 100.0f : 50.0f + (b - 1.0f) * 50.0f)));
      s->onChange = [text, name](int v) {
        float b = v <= 50 ? 0.5f + v / 100.0f : 1.0f + (v - 50) / 50.0f;
        Display::setBrightness(b);
        text(pageId(kDisplay, kBrightLabel), name(b));
      };
    }
    describe({pageId(kDisplay, kBrightLabel), pageId(kDisplay, kBrightBar)},
             GpuFsr::available() ? "Lightens or darkens the whole picture: 100% is as the game draws it."
                                 : "Needs OpenGL on your graphics card, which isn't available here.");
  }
  onOff(kDisplay, kFps, "Show the frame rate (F3)", g_showDiagnostics, [](bool on) { g_showDiagnostics = on; });
  describe({pageId(kDisplay, kFps), pageId(kDisplay, kFps + 1)},
           "Frames a second, memory and the renderer in the corner of the screen.");

  // Graphics: Interface Size (the window keeps its size while it's dragged)
  text(pageId(kGraphics, kScaleLabel), "Interface Size");
  this->scale.attach(bar(pageId(kGraphics, kScaleBar)), dynamic_cast<UiText *>(L->getElementById(pageId(kGraphics, kScaleLabel))),
                     button(pageId(kGraphics, kScaleAuto)),
                     dynamic_cast<UiText *>(L->getElementById(pageId(kGraphics, kScaleNote))), "Interface Size: ", true);
  describe({pageId(kGraphics, kScaleBar), pageId(kGraphics, kScaleLabel)},
           "How big menus and panels are, from 0.5x. This window keeps its size till you let go.");
  describe({pageId(kGraphics, kScaleAuto), pageId(kGraphics, kScaleNote)},
           "Auto: 1:1 as the original, bigger in whole steps on a big window.");
  // Art upscaling: how art drawn bigger (Interface Size over 1x, the map
  // zoomed in) looks - at once, but MMPX, made as the art loads. What it
  // looks like now is said under it (tick()'s artNote)
  text(pageId(kGraphics, kArtLabel), "Art upscaling");
  choices(kGraphics, {kArtFsr, kArtSharp, kArtSmooth, kArtMmpx, kArtStyled},
          {"FSR", "Sharp", "Smooth", "MMPX", "Stylized"}, static_cast<int>(Display::art()),
          [](int i) { Display::setArt(static_cast<Display::Art>(i)); });
  describe({pageId(kGraphics, kArtStyled), pageId(kGraphics, kArtStyled + 1)},
           "Stylized: FSR's smooth upscale with a cartoon finish - dark steady outlines, crisper detail, livelier "
           "colours. Art Sharpness sets how strong.");
  describe({pageId(kGraphics, kArtLabel)}, "How the art looks when it's drawn bigger than the original's own size.");
  describe({pageId(kGraphics, kArtFsr), pageId(kGraphics, kArtFsr + 1)},
           "FSR: AMD's upscaler on your graphics card - smooth edges, kept crisp by Art sharpness.");
  describe({pageId(kGraphics, kArtSharp), pageId(kGraphics, kArtSharp + 1)},
           "Sharp: every pixel a crisp square, as the original's art was drawn.");
  describe({pageId(kGraphics, kArtSmooth), pageId(kGraphics, kArtSmooth + 1)},
           "Smooth: the art blended as it's stretched - soft, no jagged edges.");
  describe({pageId(kGraphics, kArtMmpx), pageId(kGraphics, kArtMmpx + 1)},
           "MMPX: pixel-art upscaling that keeps outlines clean. Made as the art loads: from the next start.");
  describe({pageId(kGraphics, kArtNote)}, "What the art looks like now, and how to see the difference.");
  text(pageId(kGraphics, kSharpLabel), "Art Sharpness");
  if (UiScrollBar *s = bar(pageId(kGraphics, kSharpBar))) {
    // (FSR's sharpening in stops, 0 the sharpest: 2 at the left, 0 at the right)
    float stopsNow = static_cast<float>(std::atof(Settings::get("user", "artsharpness", "0.2").c_str()));
    s->setValue(std::clamp(static_cast<int>(std::lround((2.0f - stopsNow) * 50.0f)), 0, 100));
    s->onChange = [](int v) {
      char b[16];
      std::snprintf(b, sizeof b, "%.2f", 2.0f - v / 50.0f);
      Settings::set("user", "artsharpness", b);
      // (FSR sharpens every frame: at once)
      GpuFsr::setSharpness(2.0f - v / 50.0f);
    };
  }
  describe({pageId(kGraphics, kSharpBar), pageId(kGraphics, kSharpLabel)},
           "How crisp FSR keeps the enlarged art: further right is sharper. (FSR only.)");

  // Sound (the original's: gameopt2_MainVolumeScrollbar_click 0x58b98d, 0 is
  // silence; gameopt2_MainMenuMusicScrollbar_click 0x47ff4d)
  text(pageId(kSound, kVolumeLabel), "Main Volume");
  text(pageId(kSound, kMusicLabel), "Menu Music Volume");
  if (UiScrollBar *s = bar(pageId(kSound, kVolumeBar))) {
    s->setValue(std::clamp(100 - Settings::getInt("UI", "userAttenuation", 0) / 15, 0, 100));
    s->onChange = [](int v) {
      Settings::setInt("UI", "userAttenuation", v == 0 ? 5000 : (100 - v) * 15);
      Settings::apply();
    };
  }
  if (UiScrollBar *s = bar(pageId(kSound, kMusicBar))) {
    int top = Settings::getInt("UI", "menuMusicMaxAttenuation", -1000);
    s->setValue(std::clamp(100 - (Settings::getInt("UI", "menuMusicAttenuation", -1000) - top) / 50, 0, 100));
    s->onChange = [top](int v) {
      Settings::setInt("UI", "menuMusicAttenuation", top + (100 - v) * 50);
      Settings::apply();
    };
  }

  // Controls (the original's: scroll speed 0x606800, tool tips 0x51a559,
  // 0x51a59f; [UI] helpType)
  text(pageId(kControls, kScrollLabel), "Map Scroll Speed");
  if (UiScrollBar *s = bar(pageId(kControls, kScrollBar))) {
    s->setValue(std::clamp(static_cast<int>(std::lround(Settings::getInt("UI", "mouseScrollX", 32) * 100.0 / 120.0)), 0, 100));
    s->onChange = [](int v) {
      int step = 8 + static_cast<int>(v * 1.2);
      Settings::setInt("UI", "mouseScrollX", step);
      Settings::setInt("UI", "mouseScrollY", step);
      Settings::apply();
    };
  }
  onOff(kControls, kEdge, "Scroll the map with the mouse at the screen's edge", Display::edgeScroll(),
        [](bool on) { Display::setEdgeScroll(on); });
  describe({pageId(kControls, kEdge), pageId(kControls, kEdge + 1)},
           "Off, the map only scrolls with the arrow keys (or W A S D) - handy in a window or on a second monitor.");
  text(pageId(kControls, kTipsLabel), "Tool Tips");
  choices(kControls, {kTipsShort, kTipsLong}, {"Short", "Long"}, Settings::getInt("UI", "helpType", 0) != 0 ? 1 : 0,
          [](int i) { Settings::setInt("UI", "helpType", i); });
  describe({pageId(kControls, kTipsLabel), pageId(kControls, kTipsShort), pageId(kControls, kTipsShort + 1),
            pageId(kControls, kTipsLong), pageId(kControls, kTipsLong + 1)},
           "Short tool tips name a button; long ones say what it does.");
  // Which Game Options: this window, or the original game's side panel
  text(pageId(kControls, kStyleLabel), "Game Options");
  choices(kControls, {kStyleNew, kStyleOld}, {"New window", "Original panel"}, Features::optionsWindow ? 0 : 1,
          [](int i) {
            Features::optionsWindow = i == 0;
            Settings::saveExtra("optionswindow", Features::optionsWindow);
          });
  describe({pageId(kControls, kStyleLabel), pageId(kControls, kStyleNew), pageId(kControls, kStyleNew + 1),
            pageId(kControls, kStyleOld), pageId(kControls, kStyleOld + 1)},
           "What the disk button and Esc open in a zoo: this window, or the original game's Game Options panel.");
  text(pageId(kControls, kWinLabel), "Window Positions");
  if (UiButton *b = button(pageId(kControls, kWinReset))) {
    b->setLabel("Reset");
    b->onClick = [this] {
      this->resetPosition();
      if (this->onResetWindows)
        this->onResetWindows();
    };
  }
  describe({pageId(kControls, kWinLabel), pageId(kControls, kWinReset)},
           "Every window you have dragged goes back where the game puts it, this one too. Windows start locked: open a window's gold lock to drag it.");
  text(pageId(kControls, kDelayLabel), "Tool Tip Delay");
  text(pageId(kControls, kDurationLabel), "Tool Tip Duration");
  {
    auto told = [this] {
      if (this->onTooltips)
        this->onTooltips(Settings::getInt("UI", "tooltipDelay", 1), Settings::getInt("UI", "tooltipDuration", 3000));
    };
    if (UiScrollBar *s = bar(pageId(kControls, kDelayBar))) {
      s->setValue(std::clamp((Settings::getInt("UI", "tooltipDelay", 1) - 1) / 10, 0, 100));
      s->onChange = [told](int v) {
        Settings::setInt("UI", "tooltipDelay", v * 10 + 1);
        told();
      };
    }
    if (UiScrollBar *s = bar(pageId(kControls, kDurationBar))) {
      s->setValue(std::clamp((Settings::getInt("UI", "tooltipDuration", 3000) - 500) / 100, 0, 100));
      s->onChange = [told](int v) {
        Settings::setInt("UI", "tooltipDuration", v * 100 + 500);
        told();
      };
    }
  }
  describe({pageId(kControls, kDelayBar), pageId(kControls, kDelayLabel)},
           "How long the mouse rests on something before its tool tip shows.");
  describe({pageId(kControls, kDurationBar), pageId(kControls, kDurationLabel)},
           "How long a tool tip stays.");
  describe({pageId(kControls, kScrollBar), pageId(kControls, kScrollLabel)},
           "How fast the map scrolls at the screen's edge and with the arrow keys.");

  // Accessibility: a colour-blind filter (over the whole picture, at once)
  text(pageId(kAccess, kFilterLabel), "Colour Filter");
  choices(kAccess, {kFilterOff, kFilterP, kFilterD, kFilterT}, {"Off", "Protanopia", "Deuteranopia", "Tritanopia"},
          static_cast<int>(Display::filter()), [](int i) { Display::setFilter(static_cast<Display::Filter>(i)); });
  describe({pageId(kAccess, kFilterLabel), pageId(kAccess, kFilterOff), pageId(kAccess, kFilterOff + 1)},
           GpuFsr::available() ? "Moves colours you may not tell apart into ones you can. Off: as the game draws them."
                               : "Needs OpenGL on your graphics card, which isn't available here.");
  describe({pageId(kAccess, kFilterP), pageId(kAccess, kFilterP + 1)},
           "Protanopia: reds look dark and close to greens. Red-green differences move into blue and brightness.");
  describe({pageId(kAccess, kFilterD), pageId(kAccess, kFilterD + 1)},
           "Deuteranopia: greens and reds look alike (the most common). Their differences move into blue.");
  describe({pageId(kAccess, kFilterT), pageId(kAccess, kFilterT + 1)},
           "Tritanopia: blues and yellows look alike. Their differences move into red and green.");
  {
    auto name = [](float s) { return "Filter Strength: " + std::to_string(static_cast<int>(std::lround(s * 100))) + "%"; };
    text(pageId(kAccess, kStrengthLabel), name(Display::filterStrength()));
    if (UiScrollBar *s = bar(pageId(kAccess, kStrengthBar))) {
      s->setValue(static_cast<int>(std::lround(Display::filterStrength() * 100)));
      s->onChange = [text, name](int v) {
        Display::setFilterStrength(v / 100.0f);
        text(pageId(kAccess, kStrengthLabel), name(v / 100.0f));
      };
    }
    describe({pageId(kAccess, kStrengthLabel), pageId(kAccess, kStrengthBar)},
             "How far the colour filter moves the colours: less keeps the game's own look closer.");
  }
  text(pageId(kAccess, kCursorLabel), "Mouse Pointer");
  choices(kAccess, {kCursor1, kCursor2, kCursor3}, {"Normal", "Large", "Huge"}, Display::cursorSize() - 1,
          [](int i) { Display::setCursorSize(i + 1); });
  describe({pageId(kAccess, kCursorLabel), pageId(kAccess, kCursor1), pageId(kAccess, kCursor1 + 1),
            pageId(kAccess, kCursor2), pageId(kAccess, kCursor2 + 1), pageId(kAccess, kCursor3),
            pageId(kAccess, kCursor3 + 1)},
           "The game's own pointers, twice or three times their size: easier to follow on a big screen.");

  // The extras, each on its page
  int count[kPages] = {};
  for (Features::Switch &f : Features::all()) {
    int p = extraPage(f.name);
    if (p < 0)
      continue;
    int k = count[p]++;
    text(pageId(p, kSwitchTextFirst + k), extraName(f.name));
    if (UiButton *b = button(pageId(p, kSwitchFirst + k))) {
      b->setToggledOn(*f.value);
      bool *value = f.value;
      std::string name = f.name;
      b->onClick = [b, value, name] {
        *value = b->isToggledOn();
        Settings::saveExtra(name, *value);
        b->setToggledOn(*value); // a required map pack can refuse All Off
      };
    }
    describe({pageId(p, kSwitchFirst + k), pageId(p, kSwitchTextFirst + k)}, extraAbout(f));
  }
  // (Mods & Updates' switches - Load mods among them: HD art mods follow it
  // at once, and the list says so)
  for (int k = 0; k < count[kMods]; k++)
    if (UiButton *b = button(pageId(kMods, kSwitchFirst + k))) {
      auto was = b->onClick;
      b->onClick = [this, was] {
        if (was)
          was();
        HdArt::refresh();
        Shaders::refresh();
        this->showMods();
      };
    }
}

UiAction UiOptions::handleInputs(std::vector<Input> &inputs) {
  if (!this->layout)
    return UiAction::NONE;
  // (the Esc that opened it - the game's Esc with nothing open opens Game
  // Options - comes here the same frame: not the one that closes it)
  bool fresh = this->justOpened;
  this->justOpened = false;
  for (const Input &in : inputs) {
    if (in.event == InputEvent::KEY_ESCAPE && !fresh)
      this->close();
    if (in.type == InputType::POSITIONED) {
      this->mouseX = in.position.x;
      this->mouseY = in.position.y;
    }
  }
  // (ours, dragwindows: dragged by its title strip unless locked; its lock
  // opened or shut with a click)
  if (Features::dragWindows) {
    for (const Input &in : inputs) {
      SDL_Point p = {in.position.x, in.position.y};
      if (in.event == InputEvent::LEFT_CLICK && !this->dragging) {
        if (this->lockRect.w > 0 && SDL_PointInRect(&p, &this->lockRect)) {
          this->lockDown = true; // (opened or shut when let go on it)
          return UiAction::NONE;
        }
        SDL_Rect closeR = {0, 0, 0, 0};
        if (UiElement *c = this->layout->getElementById(kClose))
          closeR = c->getLastRect();
        if (!this->locked && SDL_PointInRect(&p, &this->lastRect) && p.y < this->lastRect.y + 20 &&
            !SDL_PointInRect(&p, &closeR)) {
          this->dragging = true;
          this->dragMoved = false;
          this->dragFromX = p.x;
          this->dragFromY = p.y;
          this->dragOffX = this->offsetX;
          this->dragOffY = this->offsetY;
        }
      } else if (in.event == InputEvent::CURSOR_MOVE && this->dragging) {
        if (std::abs(p.x - this->dragFromX) > 3 || std::abs(p.y - this->dragFromY) > 3)
          this->dragMoved = true;
        if (this->dragMoved) {
          this->offsetX = this->dragOffX + p.x - this->dragFromX;
          this->offsetY = this->dragOffY + p.y - this->dragFromY;
        }
      } else if (in.event == InputEvent::LEFT_RELEASE && this->lockDown) {
        this->lockDown = false;
        if (SDL_PointInRect(&p, &this->lockRect)) {
          this->locked = !this->locked;
          Settings::set("Windows", "lOptions", this->locked ? "1" : "0");
        }
        return UiAction::NONE;
      } else if (in.event == InputEvent::LEFT_RELEASE && this->dragging) {
        if (this->dragMoved)
          Settings::set("Windows", "wOptions", std::to_string(this->offsetX) + "," + std::to_string(this->offsetY));
        bool moved = this->dragMoved;
        this->dragging = this->dragMoved = false;
        if (moved)
          return UiAction::NONE;
      }
    }
    if (this->dragMoved)
      return UiAction::NONE;
  }
  this->layout->handleInputs(inputs);
  // What the setting under the mouse does (else the page's own words)
  if (UiText *a = dynamic_cast<UiText *>(this->layout->getElementById(kAbout))) {
    std::string said = aboutText(kPageAbout[this->page]);
    SDL_Point m = {this->mouseX, this->mouseY};
    for (const auto &[id, words] : this->about)
      if (UiElement *e = this->layout->getElementById(id))
        if (pageOf(id) == this->page && !e->isHidden()) {
          SDL_Rect r = e->getLastRect();
          if (SDL_PointInRect(&m, &r)) {
            said = aboutText(words);
            break;
          }
        }
    if (a->getText() != said)
      a->setText(said);
  }
  UiAction out = UiAction::NONE;
  if (this->closeAsked) {
    out = this->pending;
    this->pending = UiAction::NONE;
    this->finishClose();
  }
  return out;
}

void UiOptions::draw(SDL_Renderer *renderer, SDL_Rect *screen, float originX, float originY) {
  if (!this->layout)
    return;
  this->scale.update(renderer, originX, originY);
  this->tick(renderer);
  SDL_Rect r = this->layout->computeRect(renderer, screen);
  // (ours, dragwindows: where it was dragged to, its title kept on the screen)
  if (!this->settled) {
    this->settled = true;
    std::sscanf(Settings::get("Windows", "wOptions").c_str(), "%d,%d", &this->offsetX, &this->offsetY);
    this->locked = Settings::get("Windows", "lOptions") != "0"; // (locked unless opened)
  }
  if (Features::dragWindows) {
    int x = std::clamp(r.x + this->offsetX, screen->x - r.w + 60, screen->x + screen->w - 60);
    int y = std::clamp(r.y + this->offsetY, screen->y, screen->y + screen->h - 24);
    this->offsetX = x - r.x;
    this->offsetY = y - r.y;
    r.x = x;
    r.y = y;
  }
  this->lastRect = r;
  if (RenderSettings::drawsUiArt()) {
    // The Help window's frame (500 x 331) stretched to kW x kH from its
    // edges - its corners as they are, its sides and middle drawn longer
    // (each an even bevel) - and behind its two panes, in place of its
    // parchment, the Zoo Status window's zoo (ui/sharedui/zoobck: the
    // tiger, the tree and the ape behind its stripes) on that window's own
    // dark green
    Animation *frame = this->rm->getAnimation("ui/help/help/help");
    Animation *zoo = this->rm->getAnimation("ui/sharedui/zoobck/zoobck");
    const SDL_Color white = {255, 255, 255, 255};
    // (a part of the art, src in its pixels, stretched over dst)
    auto piece = [&](Animation *a, int aw, int ah, SDL_FRect src, SDL_FRect dst) {
      SDL_FPoint d[4] = {{dst.x, dst.y}, {dst.x + dst.w, dst.y}, {dst.x + dst.w, dst.y + dst.h}, {dst.x, dst.y + dst.h}};
      float u0 = src.x / aw, v0 = src.y / ah, u1 = (src.x + src.w) / aw, v1 = (src.y + src.h) / ah;
      SDL_FPoint uv[4] = {{u0, v0}, {u1, v0}, {u1, v1}, {u0, v1}};
      a->drawMapped(renderer, CompassDirection::N, 0, d, uv, white);
    };
    int fw = 0, fh = 0;
    if (frame)
      frame->queryTexture(CompassDirection::N, &fw, &fh);
    if (frame && fw > 0 && fh > 0) {
      // (cut where the frame runs even: past the left column, short of the
      // right edge; under the page's title strip, above the foot)
      const float sx[4] = {0, 186, static_cast<float>(fw - 30), static_cast<float>(fw)};
      const float sy[4] = {0, 56, static_cast<float>(fh - 31), static_cast<float>(fh)};
      const float dx[4] = {0, 186, static_cast<float>(kW - 30), static_cast<float>(kW)};
      const float dy[4] = {0, 56, static_cast<float>(kH - 31), static_cast<float>(kH)};
      for (int j = 0; j < 3; j++)
        for (int i = 0; i < 3; i++)
          piece(frame, fw, fh, {sx[i], sy[j], sx[i + 1] - sx[i], sy[j + 1] - sy[j]},
                {r.x + dx[i], r.y + dy[j], dx[i + 1] - dx[i], dy[j + 1] - dy[j]});
    }
    const SDL_Rect panes[] = {{9, 26, 170, kH - 55}, {182, 51, kW - 194, kH - 80}};
    int zw = 0, zh = 0;
    if (zoo)
      zoo->queryTexture(CompassDirection::N, &zw, &zh);
    SDL_BlendMode was;
    SDL_GetRenderDrawBlendMode(renderer, &was);
    for (const SDL_Rect &p : panes) {
      SDL_Rect clip = {r.x + p.x, r.y + p.y, p.w, p.h};
      SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
      SDL_SetRenderDrawColor(renderer, 41, 61, 44, 255);
      SDL_RenderFillRect(renderer, &clip);
      if (zoo && zw > 15 && zh > 45) {
        // (zoobck's own picture starts 15 in and 45 down: it covers the
        // whole window, its middle kept, its shape kept)
        SDL_RenderSetClipRect(renderer, &clip);
        float pw = zw - 15.0f, ph = zh - 45.0f, k = std::max(kW / pw, kH / ph);
        float cw = kW / k, ch = kH / ph * ph / k;
        piece(zoo, zw, zh, {15 + (pw - cw) / 2, 45 + (ph - ch) / 2, cw, ch},
              {static_cast<float>(r.x), static_cast<float>(r.y), static_cast<float>(kW), static_cast<float>(kH)});
        SDL_RenderSetClipRect(renderer, nullptr);
      }
    }
    SDL_SetRenderDrawBlendMode(renderer, was);
  }
  this->layout->draw(renderer, &r);
  // Its lock, beside the close button
  this->lockRect = {0, 0, 0, 0};
  if (Features::dragWindows)
    if (UiElement *c = this->layout->getElementById(kClose)) {
      SDL_Rect cr = c->getLastRect();
      if (cr.w > 0) {
        this->lockRect = WindowLock::beside(cr);
        SDL_Point m = {this->mouseX, this->mouseY};
        bool over = SDL_PointInRect(&m, &this->lockRect);
        WindowLock::draw(renderer, this->rm, this->lockRect, this->locked, over, over && this->lockDown);
      }
    }
}

void UiOptions::resetPosition() {
  this->offsetX = this->offsetY = 0;
  Settings::set("Windows", "wOptions", "");
}
