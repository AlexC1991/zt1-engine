#include "UiGameScreen.hpp"
#include "../Shows.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <functional>
#include <set>
#include <sstream>

#include "../ItemCatalog.hpp"
#include "../Research.hpp"
#include "../Fences.hpp"
#include "../ZooSim.hpp"
#include "../OriginalConstants.hpp"
#include "../Features.hpp"
#include "../Settings.hpp"
#include "../RenderSettings.hpp"
#include "../Embed.hpp"
#include "../Window.hpp"
#include "UiScrollBar.hpp"
#include <cstdio>
#include <cstring>
#include <map>
#include "../Utils.hpp"
#include "../LangText.hpp"
#include "UiButton.hpp"
#include "UiGraph.hpp"
#include "UiLayout.hpp"
#include "UiScrollingRegion.hpp"
#include "UiImage.hpp"
#include "UiListBox.hpp"
#include "UiStatusImage.hpp"
#include "UiText.hpp"
#include "WindowLock.hpp"
#include "UiSpeedControlArt.hpp"
#include <SDL3_image/SDL_image.h>

// addition: the Game Options' Extras tab. The original's gameopts.lyt has an
// About tab button with its action left out (";action=3 ;target=903"): it's
// turned on here, pointed at a page of the engine's additions
// (ui/zt1extras.lyt, made below), each switched on or off. Off, each does
// what the original does (Features.hpp)
// A building's months open, as Building Information and Zoo Status' building
// list show it (zoo.exe buildingInfoNumbers 0x488446, 0x5317c5: whole days
// since it opened, + 29.9 cut, / 30, at least 1 - Original::monthsOpen)
static int buildingMonthsOpen(const ZooSim *sim, const Fences *fences, int monthNumber,
                              const PlacedObjects::Object &o) {
  if (!sim)
    return std::max(1, monthNumber - o.openedMonth + 1);
  const int monthCount = fences ? fences->month() : monthNumber;
  return Original::monthsOpen(sim->openedDayOf(o.openedDay, o.openedMonth, monthCount), sim->today());
}

static const int kExtrasPage = 904, kExtrasTab = 1509, kExtrasFirst = 9050;
// ours: the switches a page of the tab shows (more than fit the panel go on
// further pages, turned with the game's up and down arrows under them), and
// the labels' own ids (8900 on: never a tick box's)
static const int kExtrasRows = 12, kExtrasLabelFirst = 8900;
static const int kExtrasPrev = 8890, kExtrasNext = 8891, kExtrasPageText = 8892;
static std::string extrasLabel(const std::string &name) {
  static const std::map<std::string, std::string> labels = {
      {"allunlocked", "All unlocked"},   {"fencemodes", "Fence modes"},
      {"staffpaths", "Staff on paths"},  {"smoothroutes", "Smooth walking"},
      {"maptooltips", "Map hints"},      {"elevatedpaths", "Raised paths"},
      {"pathmodes", "Path modes"},       {"hotkeys", "Extra hotkeys"},
      {"mods", "Load mods"},             {"modmenu", "Mod menu"},
      {"updatechecks", "Update checks"}, {"misnamedart", "Misnamed art"},
      {"optionswindow", "New Options"},   {"smartstack", "Windows aside"},
      {"fastforward", "Game speed"},
      {"dragwindows", "Drag windows"},   {"infocloseslists", "Info closes lists"},
      // (as the Options window names them, shortened to the panel's 100 px)
      {"tankgroundback", "Tank ground back"}, {"nofenceonpath", "No path fences"},
      {"modfilter", "Mods buy filter"},  {"custommaps", "Custom maps"},
      {"suitabilityadvice", "Exhibit advice"}, {"tankclip", "Tank edge clip"},
      {"celebrations", "Celebrations"}};
  auto it = labels.find(name);
  return it == labels.end() ? name : it->second;
}
// The Extras page's order: the new Options window's switch first (the way
// back to it from this panel), then the rest as Features lists them
static std::vector<Features::Switch *> extrasOrder() {
  std::vector<Features::Switch *> out;
  for (Features::Switch &f : Features::all())
    if (std::string(f.name) == "optionswindow")
      out.insert(out.begin(), &f);
    else if (std::string(f.name) != "infinitemoney" && std::string(f.name) != "daynight" &&
             std::string(f.name) != "gametime") // The lighting/clock additions use the Options window; the cheat uses Mods.
      out.push_back(&f);
  return out;
}
// A layout's text without the sections named (each "[Name]" to the next
// section): for the Game Options' old-PC settings, left out (below)
static std::string dropSections(const std::string &text, const std::set<std::string> &names) {
  std::string out;
  bool keep = true;
  size_t at = 0;
  while (at < text.size()) {
    size_t end = text.find('\n', at);
    end = end == std::string::npos ? text.size() : end + 1;
    std::string line = text.substr(at, end - at);
    if (!line.empty() && line[0] == '[') {
      size_t close = line.find(']');
      keep = close == std::string::npos || !names.count(line.substr(1, close - 1));
    }
    if (keep)
      out += line;
    at = end;
  }
  return out;
}
// Interface Size, on Sound & Video (an addition): its label, slider and note
// (UiScaleControl, UiOptions.hpp)
static const int kUiScaleLabel = 9030, kUiScaleBar = 9031, kUiScaleNote = 9032, kUiScaleAuto = 9033;
// Sound & Video without the old PCs' settings - the fixed window sizes
// (800 x 600, 1024 x 768, 1280 x 1024: the window is resized, maximised or
// made full screen now) and Terrain Blending's speed choices (it always
// blends) - and with Interface Size where they were
static std::string soundVideoPage(ResourceManager *rm) {
  std::string t = dropSections(rm->getGameText("ui/gameopt2.lyt"),
                               {"Video_Mode_Radio_Set", "800x600", "1024x768", "1280x1024", "Terrain_RadioSet",
                                "Terrain_Option_NoBlending", "Terrain_Option_Speed", "Terrain_Option_Balance",
                                "Terrain_Option_Quality", "Terrain_NoBlending_Label", "Terrain_Speed_Label",
                                "Terrain_Balanced_Label", "Terrain_Quality_Label", "Terrain_Option_Label"});
  // (as the volume's label and slider above it)
  const std::string gold = "forecolor=237\r\nforecolor=228\r\nforecolor=173\r\nbackcolor=100\r\nbackcolor=108\r\n"
                           "backcolor=108\r\ntransparent=1\r\nlayer=1\r\n";
  t += "\r\n[UiScale_Label]\r\ntype=UIText\r\nid=" + std::to_string(kUiScaleLabel) +
       "\r\nx=38\r\ny=250\r\ndx=110\r\ndy=fitfont\r\nfont=3452\r\nfontsize=3453\r\nborder=1\r\n" + gold +
       "\r\n[UiScale_ScrollBar]\r\ntype = UIScrollBar\r\nid = " + std::to_string(kUiScaleBar) +
       "\r\nanimation = ui/sharedui/horzslid/horzslid\r\nthumb = ui/sharedui/horzthm/horzthm\r\nx=38\r\ny=265\r\n"
       "dx=110\r\ndy=26\r\nlayer = 1\r\ntransparent=1\r\n"
       "\r\n[UiScale_Auto]\r\ntype=UIButton\r\nid=" + std::to_string(kUiScaleAuto) +
       "\r\nx=36\r\ny=294\r\nanimation=ui/sharedui/checkbx/checkbx\r\nlayer=1\r\ntransparent=1\r\nstate=2048\r\n"
       "\r\n[UiScale_Note]\r\ntype=UIText\r\nid=" + std::to_string(kUiScaleNote) +
       "\r\nx=60\r\ny=297\r\ndx=94\r\ndy=fitfont\r\nfont=3452\r\nfontsize=3453\r\nborder=0\r\n" + gold;
  return t;
}
static std::string gameOptionsWithExtras(ResourceManager *rm) {
  // (the Advanced tab's page held only old PCs' settings - video, sound and
  // mouse quality: left out with its tab, and Extras takes its place)
  std::string t = dropSections(rm->getGameText("ui/gameopts.lyt"), {"Advanced Opion Tab", "AdvancedOptionsLayout"});
  auto swap = [&t](const std::string &from, const std::string &to) {
    for (const char *nl : {"\r\n", "\n"}) {
      std::string f = from, r = to;
      for (std::string *s : {&f, &r})
        for (size_t at = 0; (at = s->find('\n', at)) != std::string::npos; at += std::strlen(nl))
          s->replace(at, 1, nl);
      size_t at = t.find(f);
      if (at != std::string::npos) {
        t.replace(at, f.size(), r);
        return true;
      }
    }
    return false;
  };
  swap("button = 1530\n", "");
  if (!swap(";state=4096\n;action=3\n;target=903", "state=4096\naction=3\ntarget=" + std::to_string(kExtrasPage)) ||
      !swap("button = 1508", "button = 1508\nbutton = " + std::to_string(kExtrasTab)))
    return t; // (a gameopts.lyt not like the shipped one: left as it is)
  // (the update's About tab sat under Advanced, at 213: in its place now)
  swap("y = 213", "y = 180");
  swap("helpid = 1544", "helpid = 0");
  t += "\r\n[ExtrasLayout]\r\ntype=UILayout\r\nlayout=ui/zt1extras.lyt\r\nstate=1\r\n";
  return t;
}
static std::string extrasPage() {
  std::string t = "[LayoutInfo]\r\nanchor=1032\r\nx=17\r\ny=6\r\ndx=171\r\ndy=440\r\nid=" +
                  std::to_string(kExtrasPage) + "\r\nlayer=1\r\n\r\n";
  const std::string gold = "forecolor=237\r\nforecolor=228\r\nforecolor=173\r\nbackcolor=100\r\nbackcolor=108\r\n"
                           "backcolor=108\r\ntransparent=1\r\nlayer=1\r\n";
  t += "[Title]\r\ntype=UIText\r\nid=9040\r\nx=29\r\ny=29\r\ndx=127\r\ndy=fitfont\r\nfont=3270\r\nfontsize=3271\r\n"
       "border=2\r\nforecolor=206\r\nforecolor=206\r\nforecolor=206\r\nbackcolor=68\r\nbackcolor=36\r\n"
       "backcolor=20\r\ntransparent=1\r\nlayer=1\r\njustify=center\r\n\r\n";
  int n = static_cast<int>(extrasOrder().size());
  int rows = std::min(n, kExtrasRows);
  for (int i = 0; i < n; i++) {
    // (below the panel's logo, 19 apart - the original's pages use 22, but
    // a page of switches and the note must fit inside the panel - each the
    // game's own tick box, its name in the panel's 100 px beside it; a page
    // after the first in the same rows, hidden until it's turned to)
    int y = 148 + (i % kExtrasRows) * 19, id = kExtrasFirst + i;
    t += "[Toggle" + std::to_string(i) + "]\r\ntype=UIButton\r\nid=" + std::to_string(id) +
         "\r\nanimation=ui/sharedui/checkbx/checkbx\r\nx=33\r\ny=" + std::to_string(y) +
         "\r\nlayer=1\r\ntransparent=1\r\nstate=2048\r\n\r\n";
    t += "[Label" + std::to_string(i) + "]\r\ntype=UIText\r\nid=" + std::to_string(kExtrasLabelFirst + i) +
         "\r\nx=57\r\ny=" + std::to_string(y + 2) + "\r\ndx=100\r\ndy=fitfont\r\nfont=3452\r\nfontsize=3453\r\n"
         "border=0\r\nalias=" + std::to_string(id) + "\r\n" + gold + "\r\n";
  }
  t += "[Note]\r\ntype=UIText\r\nid=9049\r\nx=33\r\ny=" + std::to_string(152 + rows * 19) +
       "\r\ndx=124\r\ndy=fitfont\r\nfont=3452\r\nfontsize=3453\r\nborder=0\r\njustify=center\r\n" + gold + "\r\n";
  if (n > kExtrasRows) {
    // (the pages: the game's 12 x 17 list arrows either side of "1 / 2")
    int y = 172 + rows * 19;
    t += "[PagePrev]\r\ntype=UIButton\r\nid=" + std::to_string(kExtrasPrev) +
         "\r\nanimation=ui/sharedui/arowup/arowup\r\nx=40\r\ny=" + std::to_string(y) +
         "\r\nlayer=1\r\ntransparent=1\r\n\r\n";
    t += "[PageNext]\r\ntype=UIButton\r\nid=" + std::to_string(kExtrasNext) +
         "\r\nanimation=ui/sharedui/arowdn/arowdn\r\nx=138\r\ny=" + std::to_string(y) +
         "\r\nlayer=1\r\ntransparent=1\r\n\r\n";
    t += "[PageText]\r\ntype=UIText\r\nid=" + std::to_string(kExtrasPageText) + "\r\nx=56\r\ny=" +
         std::to_string(y + 2) +
         "\r\ndx=78\r\ndy=fitfont\r\nfont=3452\r\nfontsize=3453\r\nborder=0\r\njustify=center\r\n" + gold;
  }
  return t;
}

UiGameScreen::UiGameScreen(ResourceManager *resource_manager,
                           const std::string &mainLayout,
                           const std::string &screenLayout) {
  this->resource_manager = resource_manager;
  resource_manager->provide("ui/gameopts.lyt", gameOptionsWithExtras);
  resource_manager->provide("ui/gameopt2.lyt", soundVideoPage);
  resource_manager->provide("ui/show.lyt", [](ResourceManager *rm) {
    return rm->getGameText("ui/show.lyt") +
      "\n[ShowStatus]\ntype=UIText\nid=71100\nx=12\ny=244\ndx=418\ndy=22\n"
      "font=3452\nfontsize=3453\nborder=0\ntransparent=1\nshadowtext=1\nlayer=7\n"
      "forecolor=255\nforecolor=228\nforecolor=173\n";
  });
  resource_manager->provide("ui/infocom.lyt", [](ResourceManager *rm) {
    return rm->getGameText("ui/infocom.lyt") +
      "\n[LinkedShow]\ntype=UIText\nid=71110\nx=15\ny=190\ndx=177\ndy=18\n"
      "font=3452\nfontsize=3453\nborder=0\ntransparent=1\nshadowtext=1\nlayer=7\n"
      "forecolor=255\nforecolor=228\nforecolor=173\n";
  });
  this->options = new UiOptions(resource_manager);
  this->options->onTooltips = [this](int delay, int durationMs) {
    this->tooltipDelayMs = delay;
    this->tooltipDurationMs = static_cast<Uint32>(std::max(500, durationMs));
  };
  this->options->onResetWindows = [this] {
    for (Entry &e : this->entries)
      if (e.moved) {
        e.moved = false;
        Settings::set("Windows", "w" + std::to_string(e.id), "");
      }
  };
  resource_manager->provide("ui/zt1extras.lyt", [](ResourceManager *) { return extrasPage(); });

  IniReader *mainReader = resource_manager->getIniReader(mainLayout);
  if (!mainReader) {
    SDL_Log("[GameScreen] %s not found", mainLayout.c_str());
    return;
  }
  int mainLayer = mainReader->getInt("layoutinfo", "layer", 2);
  this->hud = new UiLayout(mainReader, resource_manager);
  // addition: borrow the complete native Date text role, including localized
  // font IDs and colours. Only the optional wider box has new geometry.
  if (auto *date = dynamic_cast<UiText *>(this->hud->getElementById(1030))) {
    char empty = 0;
    this->dateTimeIni = std::make_unique<IniReader>(&empty, 0);
    for (const auto &[key, value] : mainReader->getSection(date->getName()))
      this->dateTimeIni->set("date", key, value);
    this->dateTimeIni->set("date", "x", "0");
    this->dateTimeIni->set("date", "y", "0");
    this->dateTimeIni->set("date", "dy", "fitfont");
    this->dateTimeText = std::make_unique<UiText>(this->dateTimeIni.get(), resource_manager, "date");
  }
  this->entries.push_back({this->hud, this->hud->getId(), mainLayer, false, true});

  // The HUD's panel buttons, and so the panels this screen needs
  std::set<int> targets;
  std::function<void(UiElement *)> collect = [&](UiElement *e) {
    if (UiButton *b = dynamic_cast<UiButton *>(e)) {
      if (b->getActionType() == 3 && b->getActionTarget() != 0) {
        this->panelButtons.push_back(b);
        targets.insert(b->getActionTarget());
      }
    }
    for (UiElement *c : e->getChildren())
      collect(c);
  };
  collect(this->hud);
  // Panels opened by clicking things on the map, not by a HUD button: a
  // tank filter's
  targets.insert(41); // Marine Mania show scripts and admission.
  targets.insert(46);
  targets.insert(17); // a staff member's, clicked on the map or the list
  targets.insert(1400); // a DRT base's (drt.lyt), clicked on the map
  targets.insert(6);  // an animal's, clicked on the map
  targets.insert(28); // its Zookeeper Recommendations
  targets.insert(33); // Hire Staff's info plaque (splaque.lyt)
  targets.insert(27); // Adopt Animals' info plaque (plaque.lyt)
  targets.insert(9);  // a guest's, clicked on the map
  targets.insert(21); // a stand's Building Information
  targets.insert(20); // another building's
  targets.insert(29); // the Message List (the Messages button, 1006)
  targets.insert(302); // a building's paint tab (infocomC.lyt)
  targets.insert(37);  // its colour picker (color.lyt)
  targets.insert(38);  // the Help window (help.lyt)
  targets.insert(45400); // the tutorials' reminder bar (objectve.lyt)

  // The panels are the screen layout's sub-layouts with those ids
  IniReader *screen = resource_manager->getIniReader(screenLayout);
  if (screen) {
    std::string mainLower = Utils::string_to_lower(mainLayout);
    bool keepScreen = false;
    for (const std::string &section : screen->getSections()) {
      // The message bar (ZTMessageQueue) across the top
      if (Utils::string_to_lower(screen->get(section, "type")) == "ztmessagequeue" &&
          !this->messageBar) {
        this->messageBar = new UiImage(screen, resource_manager, section);
        keepScreen = true;
        continue;
      }
      if (Utils::string_to_lower(screen->get(section, "type")) != "uilayout")
        continue;
      std::string path = screen->get(section, "layout");
      if (path.empty() || Utils::string_to_lower(path) == mainLower)
        continue;
      IniReader *reader = resource_manager->getIniReader(path);
      if (!reader)
        continue;
      int id = reader->getInt("layoutinfo", "id", 0);
      if (!targets.count(id)) {
        delete reader;
        continue;
      }
      int layer = reader->getInt("layoutinfo", "layer", 1);
      UiLayout *panel = new UiLayout(reader, resource_manager);
      this->entries.push_back({panel, id, layer, true, false});
      SDL_Log("[GameScreen] panel %d from %s", id, path.c_str());
    }
    if (!keepScreen) // the message bar reads its section as it draws
      delete screen;
  }

  // Drawn by layer; the screen file's order breaks ties
  std::stable_sort(this->entries.begin(), this->entries.end(),
                   [](const Entry &a, const Entry &b) { return a.layer < b.layer; });
  // Each window's drag handle (the original's alias=-1 element), and where
  // the player left it last time (the dragwindows extra: zoo.ini [Windows])
  for (Entry &e : this->entries) {
    if (!e.panel)
      continue;
    if (IniReader *ini = e.layout->getIniReader())
      for (const std::string &s : ini->getSections()) {
        if (!e.handle && ini->get(s, "alias") == "-1")
          e.handle = ini->getInt(s, "id", 0);
        if (!e.closeId && Utils::string_to_lower(ini->get(s, "animation")).find("ui/sharedui/close/close") == 0)
          e.closeId = ini->getInt(s, "id", 0);
      }
    // (locked unless the player opened it: zoo.ini [Windows] l<id>=0)
    e.locked = Settings::get("Windows", "l" + std::to_string(e.id)) != "0";
    std::string at = Settings::get("Windows", "w" + std::to_string(e.id));
    int x = 0, y = 0;
    if (Features::dragWindows && std::sscanf(at.c_str(), "%d,%d", &x, &y) == 2) {
      e.moved = true;
      e.moveX = x;
      e.moveY = y;
    }
  }

  this->setupBuyPanels();
  this->setupTerraform();
  this->setupResearch();
  this->setupZooStatus();
  this->setupScenarioPage();
  this->setupExhibits();
  this->setupFilterPanel();
  this->setupStaffPanels();
  this->setupLists();
  this->setupColours();
  this->setupAnimalPanel();
  this->setupHelp();
  this->setupShows();
  this->setupGameOptions();

  // The bulldozer (Clear Objects) is a toggle that starts off: only the
  // view toggles (trees, guests, buildings) start on
  if (UiButton *b = dynamic_cast<UiButton *>(this->getElementById(1028)))
    b->setToggledOn(false);

  // The game menu's Main Menu and Exit Game (their layout gives them no
  // action; the game does it)
  for (UiAction a : {UiAction::GAME_LOAD, UiAction::GAME_SAVE, UiAction::GAME_MAIN_MENU, UiAction::GAME_EXIT})
    if (UiButton *b = dynamic_cast<UiButton *>(
            this->getElementById(static_cast<int>(a))))
      b->onClick = [this, a] { this->pendingAction = a; };

  if (IniReader *xpac = resource_manager->getIniReader("ui/xpac.lyt")) {
    this->filter.packs = new UiLayout(xpac, resource_manager);
    this->filter.packsList = dynamic_cast<UiListBox *>(this->filter.packs->getElementById(22900));
    // (with Mods: the same layout from xpac.lyt's own text, its box, list
    // and background a row taller - the original's margin under the rows kept)
    std::string text = resource_manager->readResources({"ui/xpac.lyt"})["ui/xpac.lyt"];
    if (!text.empty()) {
      // (a row as the game's list has it)
      int row = this->filter.packsList ? this->filter.packsList->getRowHeight() : 18;
      int boxDy = xpac->getInt("LayoutInfo", "dy", 94), listDy = xpac->getInt("list", "dy", 90);
      std::string out, section, line;
      std::istringstream in(text);
      while (std::getline(in, line)) {
        std::string t = Utils::string_to_lower(line);
        t.erase(std::remove_if(t.begin(), t.end(), ::isspace), t.end());
        if (!t.empty() && t[0] == '[') {
          section = t;
          out += line + "\n";
          if (section == "[background]")
            out += "dx=" + std::to_string(xpac->getInt("LayoutInfo", "dx", 127)) + "\ndy=" +
                   std::to_string(boxDy + row) + "\n";
          continue;
        }
        if (t.rfind("dy=", 0) == 0 && (section == "[layoutinfo]" || section == "[list]"))
          line = "dy=" + std::to_string((section == "[list]" ? listDy : boxDy) + row);
        out += line + "\n";
      }
      this->filter.withModsIni = new IniReader(out.data(), out.size());
      this->filter.withMods = new UiLayout(this->filter.withModsIni, resource_manager);
      this->filter.withModsList = dynamic_cast<UiListBox *>(this->filter.withMods->getElementById(22900));
    }
    this->filter.layout = this->filter.packs;
    this->filter.list = this->filter.packsList;
  }
}

std::string UiGameScreen::filterLabel(int choice) const {
  return choice == kFilterMods ? "Mods" : this->resource_manager->getString(22900 + choice);
}

void UiGameScreen::toggleGameMenu() {
  if (this->filter.open) {
    this->filter.open = false;
    return;
  }
  if (this->isPanelOpen(5)) {
    this->setPanelOpen(5, false);
    for (UiButton *b : this->panelButtons)
      if (b->getActionTarget() == 5)
        b->setToggledOn(false);
  } else {
    this->showPanel(5, "");
  }
}

void UiGameScreen::openFilter(int panelId) {
  // (Mods under the packs, in the box a row taller, for the buy panels
  // with the switch on; Research has none)
  bool mods = Features::modFilter && panelId != 15 && this->filter.withModsList;
  this->filter.layout = mods ? this->filter.withMods : this->filter.packs;
  this->filter.list = mods ? this->filter.withModsList : this->filter.packsList;
  if (!this->filter.list)
    return;
  this->filter.list->clear();
  for (int id = 22900; id <= 22903; id++)
    this->filter.list->addItem(id);
  if (mods)
    this->filter.list->addItem(this->filterLabel(kFilterMods));
  auto had = this->filterChoice.find(panelId);
  if (!mods && had != this->filterChoice.end() && had->second == kFilterMods)
    this->setFilter(panelId, 0); // (switched off since: All)
  this->filter.open = true;
  this->filter.owner = panelId;
  // The current choice is highlighted
  auto it = this->filterChoice.find(panelId);
  this->filter.list->setSelectedIndex(it == this->filterChoice.end() ? 0
                                                                     : it->second);
}

// zoo.exe 0x5974_click 0x4e4c4d -> ui_buya_4f06a5: one choice for every
// panel (0x638d4c) - each panel's filter button (Adopt 2005, Objects 3005,
// Build Exhibit 3204, Hire Staff 3601, Research 4021) shows it, every list
// is filled again (0x4f0a49, 0x4f0a84, 0x4f0abf; Research 0x4eb3f7), and
// Hire Staff starts over: its pick, its type and its facing 0 (0x63e470,
// 0x63e474, 0x63e46c). (Mods, the modfilter addition, is the buy panels'
// only: Research, which hasn't it, keeps its pack)
void UiGameScreen::setFilter(int panelId, int choice) {
  this->filter.open = false;
  std::set<int> panels = {panelId};
  for (const BuyPanel &p : this->buyPanels)
    panels.insert(p.id);
  if (choice != kFilterMods)
    panels.insert(15);
  for (int id : panels)
    this->filterChoice[id] = choice;
  for (BuyPanel &p : this->buyPanels)
    if (p.id == 10) {
      p.facing = 0;
      p.picked.clear();
    }
  // Adopt Animals' list starts at its first animal again whenever the filter
  // is chosen (measured in the original: a pick made under one filter isn't
  // kept by the next - nor when coming back - the list is filled again and
  // row 0 is selected; adoptListFill 0x4e334b, which keeps a row per
  // filter in its maps 0x63bb40 and 0x63bb30, buya_Animals_hover 0x4e27a9)
  for (BuyPanel &p : this->buyPanels)
    if (p.id == 3)
      p.picked.clear();
  for (Entry &e : this->entries) {
    if (!e.panel || !panels.count(e.id))
      continue;
    // The button shows the choice
    std::function<void(UiElement *)> label = [&](UiElement *el) {
      if (UiButton *b = dynamic_cast<UiButton *>(el))
        if (b->getActionType() == 1 && b->getActionTarget() == 152)
          b->setLabel(this->filterLabel(choice));
      for (UiElement *c : el->getChildren())
        label(c);
    };
    label(e.layout);
  }
  for (BuyPanel &p : this->buyPanels)
    if (panels.count(p.id)) {
      p.category.clear(); // list again
      this->refreshBuyPanel(p);
    }
  if (panels.count(15))
    this->refreshResearch();
}

static std::string formatPrice(int amount) {
  std::string digits = std::to_string(amount);
  for (int i = static_cast<int>(digits.size()) - 3; i > 0; i -= 3)
    digits.insert(i, ",");
  return "$" + digits;
}
// A cost shown in a panel as the original sets it (zoo.exe 0x40ed88 with its
// grouping flag false, through 0x40eca1: GetCurrencyFormatA with
// initLocale's 0 decimals 0x5358b7 and the grouping turned off): whole
// dollars, cut toward zero (the callers' fistp with the chop rounding mode),
// no thousands commas - "$1000". The buy panels' costs (2014 0x4e3285, 3016
// 0x4e42b8, 3207 0x4dacb1, the hire cost 3611 0x4e6a92), a keeper's salary
// (3503 0x47108a) and the tank panel's (0x5ae509: 4871, 4872, 4877, 4882)
// all are; only the HUD's cash groups its digits (0x40ee3d)
static std::string formatCost(int amount) {
  return "$" + std::to_string(amount);
}

void UiGameScreen::setupBuyPanels() {
  // Panel id, and its title, name and price texts (from the layouts)
  struct Def {
    int id, title, name, cost, duties;
    const char *category;
  };
  const Def defs[] = {{3, 2023, 2043, 2014, 0, ""},        // buya.lyt
                      {4, 3061, 3058, 3016, 0, ""},        // buyobj.lyt
                      {8, 3261, 3263, 3207, 0, ""},        // buldhab.lyt
                      {10, 0, 3620, 3611, 3613, "staff"}}; // personel.lyt (a pick: zoo.exe personel_Staff_click 0x4e69b8 - placed by the cursor, setPlaceItem)
  ItemCatalog &catalog = ItemCatalog::get();
  catalog.load(this->resource_manager);

  // Habitat and continent pictures (ui/infoimg.cfg [Images]: id = image)
  if (IniReader *images = this->resource_manager->getIniReader("ui/infoimg.cfg")) {
    for (int id = 9400; id < 9700; id++) {
      std::string path = images->get("images", std::to_string(id));
      if (!path.empty())
        this->infoImages[id] = path;
    }
    delete images;
  }

  for (const Def &d : defs) {
    UiLayout *layout = nullptr;
    for (Entry &e : this->entries)
      if (e.panel && e.id == d.id)
        layout = e.layout;
    if (!layout)
      continue;
    BuyPanel p;
    p.id = d.id;
    p.layout = layout;
    switch (d.id) {
    case 3: // buya.lyt
      p.male = 2000, p.female = 2001, p.info = 2050;
      p.rotateLeft = 2054, p.rotateRight = 2055;
      p.location = 2045, p.habitat = 2047, p.habitatFrame = 2046;
      p.pref = 2060, p.prefFrame = 2070;
      p.capacityLabel = 2090, p.capacity = 2091, p.era = 2082;
      break;
    case 4: // buyobj.lyt
      p.rotateLeft = 3054, p.rotateRight = 3055;
      break;
    case 8: // buldhab.lyt
      p.rotateLeft = 3258, p.rotateRight = 3259;
      p.location = 3283, p.habitat = 3281, p.habitatFrame = 3280;
      p.era = 3284, p.fenceLabel = 3296, p.fenceType = 3299, p.gate = 3240;
      break;
    case 10: // personel.lyt
      p.info = 3699, p.rotateLeft = 3676, p.rotateRight = 3677;
      break;
    }
    p.fixedCategory = d.category;
    p.title = d.title ? dynamic_cast<UiText *>(layout->getElementById(d.title)) : nullptr;
    p.name = dynamic_cast<UiText *>(layout->getElementById(d.name));
    p.cost = dynamic_cast<UiText *>(layout->getElementById(d.cost));
    p.duties = d.duties ? dynamic_cast<UiText *>(layout->getElementById(d.duties))
                        : nullptr;
    std::function<void(UiElement *)> scan = [&](UiElement *e) {
      if (auto *r = dynamic_cast<UiScrollingRegion *>(e))
        p.region = r;
      // Tabs: buttons whose stringData is a category items are listed in
      if (auto *b = dynamic_cast<UiButton *>(e))
        if (!b->getStringData().empty() &&
            !catalog.inCategory(b->getStringData(), false).empty())
          p.tabs.push_back(b);
      // The content filter (expansion packs) starts on "All"
      if (auto *b = dynamic_cast<UiButton *>(e))
        if (b->getActionType() == 1 && b->getActionTarget() == 152)
          b->setLabel(this->resource_manager->getString(22900));
      if (dynamic_cast<UiLayout *>(e) && e != layout)
        return;
      for (UiElement *c : e->getChildren())
        scan(c);
    };
    scan(layout);
    if (!p.region)
      continue;
    this->buyPanels.push_back(p);
  }
  for (BuyPanel &p : this->buyPanels) {
    BuyPanel *self = &p;
    // (a pick: kept for its tab, its name and cost shown, held with the
    // panel's facing - zoo.exe buyobj_Objects_click 0x4e40f4: the tab's
    // pick by index and type, 0x63bb88 and 0x63bb70, 0x4e4289, 0x4e42f9,
    // setPlaceItem 0x4f189a, 0x4df56d with 0x63bba8)
    p.region->onSelect = [this, self](int index) {
      if (index >= 0 && index < (int)self->items.size())
        self->picked[self->category] = self->items[index]->file;
      // An animal picked: ready to put down at once, the gender last chosen
      // (male to start; the gender buttons change it)
      if (self->id == 3) {
        this->animalPick.clear();
        if (self->category == "animals" && index >= 0 && index < (int)self->items.size()) {
          this->animalPick = self->items[index]->file;
          for (int sex = 0; sex < 2; sex++)
            if (UiButton *g = dynamic_cast<UiButton *>((sex ? self->female : self->male)
                                                           ? self->layout->getElementById(sex ? self->female : self->male)
                                                           : nullptr))
              g->setToggledOn((sex == 1) == this->animalPickFemale);
        }
      }
      // A staff member picked while the info plaque is up: the plaque
      // shows that one (zoo.exe personel_Staff_click 0x4e69b8: 0xe73 on,
      // flag 8 - 0x481648 with the type)
      if (self->id == 10)
        if (UiButton *plaque = dynamic_cast<UiButton *>(self->info ? self->layout->getElementById(self->info) : nullptr))
          if (plaque->isToggledOn() && index >= 0 && index < (int)self->items.size() && this->staff) {
            const int t = this->staff->typeOfFile(self->items[index]->file);
            if (t >= 0)
              this->showStaffPlaque(this->staff->types()[t].file);
          }
      // An animal picked while Adopt Animals' info plaque is up: the
      // plaque shows that one (zoo.exe buya_Animals_click 0x4e2cf8: 0x802
      // on, flag 8 - 0x483157 with the animal)
      if (self->id == 3 && self->category == "animals")
        if (UiButton *plaque = dynamic_cast<UiButton *>(self->info ? self->layout->getElementById(self->info) : nullptr))
          if (plaque->isToggledOn() && index >= 0 && index < (int)self->items.size())
            this->showAdoptPlaque(self->items[index]->file);
      // Picking a fence leaves gate mode (zoo.exe buldhab_Objects_click
      // 0x4da916: the tab's pick kept by index, 0x63baa8, the gate button
      // put away on that tab, uiTurnOff 0xca8, the item handed to the zoo
      // view, setPlaceItem 0x4f189a)
      if (self->id == 8 && index >= 0 && self->gate)
        if (UiButton *g = dynamic_cast<UiButton *>(self->layout->getElementById(self->gate)))
          g->setToggledOn(false);
      this->showItem(*self, index);
    };
    // Hire Staff's info plaque, a toggle (state=2048): on, the plaque for
    // the staff member held (zoo.exe personel_StaffInfoPlaque_click 0x4816c6:
    // the zoo view's placing list's first thing, its type - the DRT base's,
    // 0x1f5f, its helicopter's, 0x254e - if a staff type, 0x481648); off,
    // the plaque shut (staffPlaque_close 0x4816a9: 0x481648(0))
    if (p.id == 10 && p.info)
      if (UiButton *plaque = dynamic_cast<UiButton *>(p.layout->getElementById(p.info))) {
        plaque->setToggledOn(false);
        plaque->setGreyWhenOff(true);
        plaque->onClick = [this, plaque] {
          if (!plaque->isToggledOn()) {
            this->showStaffPlaque("");
            return;
          }
          const int t = this->staff ? this->staff->typeOfFile(this->staffToolFile()) : -1;
          if (t >= 0)
            this->showStaffPlaque(this->staff->types()[t].file);
        };
      }
    // Adopt Animals' info plaque (InfoPlaque, 2050), a toggle (state=2048):
    // on, the plaque for the animal held (zoo.exe buya_InfoPlaque_click
    // 0x4833e3: the zoo view's placing list's first thing, if an animal,
    // 0x483157 then shown, 0x483463); off, the plaque hidden
    // (buya_InfoPlaque_off 0x618d57 -> 0x4440d6: uiFind 0x1b, slot 0x48)
    if (p.id == 3 && p.info)
      if (UiButton *plaque = dynamic_cast<UiButton *>(p.layout->getElementById(p.info))) {
        plaque->setToggledOn(false);
        plaque->setGreyWhenOff(true);
        plaque->onClick = [this, plaque] {
          if (!plaque->isToggledOn()) {
            this->showAdoptPlaque("");
            return;
          }
          bool female = false;
          const std::string held = this->animalToolFile(female);
          if (!held.empty())
            this->showAdoptPlaque(held);
        };
      }
    // Buy Habitat's gate button: a toggle (gate mode)
    if (p.id == 8 && p.gate)
      if (UiButton *g = dynamic_cast<UiButton *>(p.layout->getElementById(p.gate))) {
        g->setToggle(true);
        g->setToggledOn(false); // (off to start: toggles start on)
        g->onClick = [this, g] { this->setGateMode(g->isToggledOn()); };
      }
    // Adopt Animals: the male or female button takes the animal picked
    // (zoo.exe buya_Buttons_click 0x4e3ec3: the choice kept, adoptListFill
    // 0x4e334b, which ends handing the picked animal to the zoo view)
    for (int sex = 0; sex < 2; sex++)
      if (UiButton *b = dynamic_cast<UiButton *>(
              (sex ? p.female : p.male) ? p.layout->getElementById(sex ? p.female : p.male) : nullptr))
        b->onClick = [this, self, sex] {
          int i = self->region ? self->region->getSelected() : -1;
          if (i >= 0 && i < (int)self->items.size() && self->category == "animals") {
            this->animalPick = self->items[i]->file;
            this->animalPickFemale = sex == 1;
          }
          // (one gender lit at a time)
          for (int other = 0; other < 2; other++)
            if (UiButton *g = dynamic_cast<UiButton *>((other ? self->female : self->male)
                                                           ? self->layout->getElementById(other ? self->female : self->male)
                                                           : nullptr))
              g->setToggledOn(other == sex);
        };
    // Right turns it clockwise (SE, SW, NW, NE), left back
    if (UiButton *left = dynamic_cast<UiButton *>(
            p.rotateLeft ? p.layout->getElementById(p.rotateLeft) : nullptr))
      left->onClick = [this, self] {
        if (self->category == "paths" && this->walkwayHeight)
          this->walkwayHeight(-1);
        else
          this->rotateItems(*self, -1);
      };
    if (UiButton *right = dynamic_cast<UiButton *>(
            p.rotateRight ? p.layout->getElementById(p.rotateRight) : nullptr))
      right->onClick = [this, self] {
        if (self->category == "paths" && this->walkwayHeight)
          this->walkwayHeight(+1);
        else
          this->rotateItems(*self, +1);
      };
    this->refreshBuyPanel(p);
  }
}

// The chosen tab's category fills the grid (its last pick picked again)
void UiGameScreen::refreshBuyPanel(BuyPanel &p) {
  std::string category = p.fixedCategory;
  for (UiButton *tab : p.tabs)
    if (tab->isToggledOn())
      category = tab->getStringData();
  if (category.empty() || category == p.category)
    return;
  p.category = category;
  // The panel's title is the chosen tab's name ("Creatures", "Buildings"),
  // as each tab's click sets it: Adopt's Animals by the content filter's
  // choice (zoo.exe buya_AnimalTab_click 0x4e3bc1: 0x638d4c - 0 2025, 1
  // and 3 2023, 2 2024 - its helpids in that order, 0x4e3842), Shelters
  // 6207 (buya_SheltersTab_click 0x4e60e8), Toys 6205 (buya_ToysTab_click
  // 0x4e6366), Show Objects 2077 (0x81c_click 0x4e6414 - not its helpid,
  // 2076); Objects' Buildings 6080 and Scenery 6081 (buyobj_BuildingTab_click
  // 0x4e4951, buyobj_SceneryTab_click 0x4e5090: 0x4e4890); Build Exhibit's
  // Fences 6203, Paths 6204, Foliage 6201, Rocks 6206 (buldhab_FenceTab_click
  // 0x4db58e, buldhab_PathTab_click 0x4dda0f, buldhab_FoliageTab_click
  // 0x4de09d, buldhab_RocksTab_click 0x4de232: buildListSelect 0x4db2ec)
  auto choice = this->filterChoice.find(p.id);
  static const std::map<std::pair<int, std::string>, int> titles = {
      {{3, "shelters"}, 6207}, {{3, "toys"}, 6205},  {{3, "showtoys"}, 2077}, {{4, "structures"}, 6080},
      {{4, "scenery"}, 6081},  {{8, "fence"}, 6203}, {{8, "paths"}, 6204},    {{8, "foliage"}, 6201},
      {{8, "rocks"}, 6206}};
  for (UiButton *tab : p.tabs)
    if (tab->isToggledOn() && p.title) {
      auto fixed = titles.find({p.id, category});
      p.title->setText(this->resource_manager->getString(
          fixed != titles.end() ? fixed->second
                                : tab->getHelpId(choice == this->filterChoice.end() ? 0 : choice->second)));
    }
  p.items.clear();
  for (const CatalogItem *item : ItemCatalog::get().inCategory(category))
    if (this->passesFilter(p.id, item->expansion, item->fromMod))
      p.items.push_back(item);
  std::vector<UiScrollingRegion::Item> cells;
  for (const CatalogItem *item : p.items)
    cells.push_back({this->facingIcon(p, item), item->file});
  p.region->setItems(cells);
  // (the item last picked on this tab, else the first: zoo.exe
  // adoptListFill 0x4e334b looks the tab up in its picks, 0x63bb18, finds
  // that type in the new list and selects it, 0x4e9fea)
  int pick = p.items.empty() ? -1 : 0;
  auto had = p.picked.find(category);
  if (had != p.picked.end())
    for (int i = 0; i < (int)p.items.size(); i++)
      if (p.items[i]->file == had->second)
        pick = i;
  p.region->setSelected(pick);
  this->showItem(p, pick);
  // "No items to display." (ZUPDATE's NoItemText) over an empty list
  for (int id : {2009, 3009, 3609})
    if (UiElement *none = p.layout ? p.layout->getElementById(id) : nullptr)
      none->setHidden(!p.items.empty());
}

// An item's icon for the current facing (items with fewer icons than
// facings, like the animals, keep theirs)
const std::string &UiGameScreen::facingIcon(const BuyPanel &panel, const CatalogItem *item) const {
  if (item->icons.empty())
    return item->icon;
  return item->icons[panel.facing % item->icons.size()];
}

bool UiGameScreen::selectBuyItem(int panel, const std::string &file) {
  for (BuyPanel &p : this->buyPanels) {
    if (p.id != panel || !p.region)
      continue;
    for (int i = 0; i < (int)p.items.size(); i++)
      if (p.items[i]->file == file) {
        p.region->setSelected(i);
        p.picked[p.category] = file;
        this->showItem(p, i);
        return true;
      }
  }
  return false;
}

// zoo.exe's comparators for the columns (0x616dcf visitors, 0x616e09 months,
// 0x5a4fa8 profit, 0x5a5244 profit a month: each one's flag, [0x63bbc4 +
// column], picks smaller-first when set, bigger-first when clear). The name
// column's sort, 0x4d7ae3, never reads its flag: always A to Z (ours: by
// kind, then name)
bool UiGameScreen::commerceFirst(const CommerceRow &a, const CommerceRow &b, int column, bool up) {
  switch (column) {
  case 1: return up ? a.months < b.months : a.months > b.months;
  case 2: return up ? a.visitors < b.visitors : a.visitors > b.visitors;
  case 3: return up ? a.profit < b.profit : a.profit > b.profit;
  case 4: return up ? a.average < b.average : a.average > b.average;
  default: return a.type != b.type ? a.type < b.type : a.name < b.name;
  }
}

std::string UiGameScreen::selectedBuyItem(int panel) const {
  for (const BuyPanel &p : this->buyPanels)
    if (p.id == panel && p.region) {
      const int i = p.region->getSelected();
      if (i >= 0 && i < (int)p.items.size())
        return p.items[i]->file;
    }
  return "";
}

void UiGameScreen::setWalkwayLabel(const std::string &text) {
  if (text == this->walkwayText)
    return;
  this->walkwayText = text;
  for (BuyPanel &p : this->buyPanels)
    if (p.id == 8 && p.category == "paths" && p.fenceLabel)
      if (UiText *t = dynamic_cast<UiText *>(p.layout->getElementById(p.fenceLabel)))
        if (!t->isHidden())
          t->setText(text);
}

// A panel's rotate buttons (zoo.exe buya_RotateRightButton_click 0x48e40f,
// buya_RotateLeftButton_click 0x618c9c; buyobj 0x4e5134, 0x589cf4; buldhab
// 0x5a60d2, 0x610913): that panel's facing one on (or back), 3 to 0 and 0
// to 3, and its list's icons drawn facing that way - the other panels'
// left as they are
void UiGameScreen::rotateItems(BuyPanel &p, int step) {
  p.facing = ((p.facing + step) % 4 + 4) % 4;
  for (int i = 0; i < (int)p.items.size(); i++)
    p.region->setItemIcon(i, this->facingIcon(p, p.items[i]));
}

// ours (the hotkeys extra): Q / E turn the thing held - its panel's facing
void UiGameScreen::rotateHeld(int step) {
  int cost = 0, facing = 0;
  if (this->objectToolFile(cost, facing).empty())
    return;
  for (BuyPanel &p : this->buyPanels)
    if (this->isPanelOpen(p.id) && p.region && p.region->getSelected() >= 0 &&
        p.region->getSelected() < (int)p.items.size() &&
        this->objectToolFile(cost, facing) == p.items[p.region->getSelected()]->file) {
      this->rotateItems(p, step);
      return;
    }
}

void UiGameScreen::showItem(BuyPanel &p, int index) {
  const CatalogItem *item =
      index >= 0 && index < (int)p.items.size() ? p.items[index] : nullptr;
  if (p.name)
    p.name->setText(item ? item->name : "");
  if (p.cost)
    p.cost->setText(item && item->cost ? formatCost(item->cost) : "");
  if (p.duties)
    p.duties->setText(item && item->dutiesTextId
                          ? this->resource_manager->getString(item->dutiesTextId)
                          : "");
  this->showDetails(p, item);
}

// What shows under the grid, per tab (measured against the original):
// animals: male / info / female, continent, habitat and favourite object;
// shelters, toys, buildings, scenery: rotate arrows (shelters also their
// capacity); foliage and rocks: rotate arrows, continent and habitat;
// fences: the (greyed) gate button and the fence type; paths: nothing;
// staff: info
void UiGameScreen::showDetails(BuyPanel &p, const CatalogItem *item) {
  const std::string &c = p.category;
  auto element = [&](int id) -> UiElement * {
    return id ? p.layout->getElementById(id) : nullptr;
  };
  auto show = [&](int id, bool on) {
    if (UiElement *e = element(id))
      e->setHidden(!on);
  };
  auto image = [&](int id, int frame, const std::string &path) {
    if (UiImage *img = dynamic_cast<UiImage *>(element(id))) {
      img->setImage(path);
      img->setHidden(path.empty());
    }
    show(frame, !path.empty());
  };
  auto infoImage = [&](int id) {
    auto it = this->infoImages.find(id);
    return it == this->infoImages.end() ? std::string() : it->second;
  };

  const bool animals = c == "animals";
  const bool rotates = c == "shelters" || c == "toys" || c == "showtoys" ||
                       c == "structures" || c == "scenery" || c == "foliage" ||
                       c == "rocks";
  const bool places = animals || c == "foliage" || c == "rocks";
  show(p.male, animals);
  show(p.female, animals);
  show(p.info, animals || c == "staff");
  // Paths: the walkway height arrows, for the path types that can be raised
  const bool raises = c == "paths" && item && this->walkwayHeight && this->canRaise &&
                      this->canRaise(item->file);
  // Hire Staff: the rotate arrows (0xe5c, 0xe5d) for the DRT base only -
  // its pick enables and shows them (zoo.exe personel_Staff_click 0x4e69b8,
  // type 0x1f5f -> 0x4e58f9: flag 0x10 cleared, slot 0x4c); any other pick,
  // or none, switches them off and hides them (0x4e65ab: flag 0x10, slot
  // 0x48; personel_Staff_off 0x4e6711)
  bool drtBase = false;
  if (p.id == 10 && item && this->staff) {
    const int t = this->staff->typeOfFile(item->file);
    drtBase = t >= 0 && this->staff->types()[t].kind == Staff::Kind::Helicopter;
  }
  show(p.rotateLeft, rotates || raises || drtBase);
  show(p.rotateRight, rotates || raises || drtBase);
  if (p.id == 10)
    for (int id : {p.rotateLeft, p.rotateRight})
      if (UiElement *e = element(id))
        e->setDisabled(!drtBase);
  // Adopt's era (2082): on the Animals tab, the animal's cEra's name, or
  // hidden and switched off for one without (zoo.exe 0x4e2f17: the tab set
  // 0x81a's choice 0x81b, the type's +0x1f8)
  {
    int era = 0;
    if (p.id == 3 && animals && item && this->animals) {
      const int t = this->animals->typeOfFile(item->file);
      if (t >= 0)
        era = this->animals->types()[t].era;
    }
    show(p.era, era != 0);
    if (UiElement *e = element(p.era))
      e->setDisabled(era == 0);
    if (era)
      if (UiText *t = dynamic_cast<UiText *>(element(p.era)))
        t->setText(this->resource_manager->getString(era));
  }
  // Hire Staff's Salary and Duties labels and the name: shown with a pick
  // (zoo.exe personel_Staff_click 0x4e69b8 -> 0x4e6b43, slot 0x4c), hidden
  // when it's let go (personel_Staff_off 0x4e6711 -> 0x4e66bf, slot 0x48)
  if (p.id == 10)
    for (int id : {3610, 3612, 3620})
      show(id, item != nullptr);

  image(p.location, 0, places && item ? infoImage(item->locationId) : "");
  image(p.habitat, p.habitatFrame,
        places && item ? infoImage(item->habitatId) : "");
  image(p.pref, p.prefFrame, animals && item ? item->prefIcon : "");

  bool shelter = c == "shelters" && item && item->capacity > 0;
  show(p.capacityLabel, shelter);
  show(p.capacity, shelter);
  if (shelter)
    if (UiText *t = dynamic_cast<UiText *>(element(p.capacity)))
      t->setText(std::to_string(item->capacity));

  bool fence = c == "fence";
  show(p.fenceLabel, fence || raises);
  if (raises)
    if (UiText *t = dynamic_cast<UiText *>(element(p.fenceLabel)))
      t->setText(this->walkwayText);
  // (the fence tab's own words back)
  if (UiText *t = dynamic_cast<UiText *>(element(p.fenceLabel))) {
    if (this->fenceLabelText.empty() && !raises)
      this->fenceLabelText = t->getText();
    if (fence && !this->fenceLabelText.empty())
      t->setText(this->fenceLabelText);
  }
  show(p.fenceType, fence && item);
  if (fence && item)
    if (UiText *t = dynamic_cast<UiText *>(element(p.fenceType)))
      t->setText(this->resource_manager->getString(
          item->showFence ? 3299
          : item->members.count("habitatfences") ? 3298
                                                  : 3297));
  show(p.gate, fence);
  // Enabled while there's an exhibit to put a gate on
  if (UiElement *gate = element(p.gate))
    gate->setDisabled(!this->exhibitsExist || !this->exhibitsExist());
}

// Hire Staff's info plaque (splaque.lyt, 33): zoo.exe 0x481648 - the staff
// type kept (0x63cf9c; the same one again changes nothing), the panel
// (uiFind 0x21) filled by 0x481768 and shown (slot 0x4c), or for none
// hidden (slot 0x48). 0x481768: 40001 the type's name (its slot 0x20,
// cNameID, through setTextId slot 0xc8), 40002 its cPlaqueImageName
// (+0x1c0, read at 0x4c38d8), then 0x481751 -> 0x48173e -> 0x4f0264: 40003
// the lang DLLs' TEXT resource its cGeneralInfoTextName names (+0x1cc,
// read at 0x4c3896; 0x4f00f6). The DRT's is its helicopter's (type 0x254e,
// personel_StaffInfoPlaque_click 0x4816c6)
void UiGameScreen::showStaffPlaque(const std::string &file) {
  if (file == this->staffPlaqueFile)
    return;
  this->staffPlaqueFile = file;
  UiLayout *z = nullptr;
  for (Entry &e : this->entries)
    if (e.panel && e.id == 33)
      z = e.layout;
  if (file.empty() || !z) {
    this->setPanelOpen(33, false);
    return;
  }
  IniReader *ai = this->resource_manager->getIniReader(file);
  // (per subtype: the male's, as the type the panel places - the female
  // sections have none)
  auto str = [&](const char *key) {
    std::string v = ai ? ai->get("m/characteristics/strings", key) : "";
    return v.empty() && ai ? ai->get("characteristics/strings", key) : v;
  };
  int nameId = 0;
  if (ai) {
    nameId = ai->getInt("m/characteristics/integers", "cnameid", 0);
    if (!nameId)
      nameId = ai->getInt("characteristics/integers", "cnameid", 0);
  }
  if (UiText *name = dynamic_cast<UiText *>(z->getElementById(40001)))
    name->setText(nameId ? this->resource_manager->getString(nameId) : "");
  if (UiImage *image = dynamic_cast<UiImage *>(z->getElementById(40002)))
    image->setImage(Utils::string_to_lower(str("cplaqueimagename")));
  // (its own line breaks kept, each line wrapped to the box at word breaks
  // - measured: the scientist's "...specialize" ends a line short)
  if (UiText *text = dynamic_cast<UiText *>(z->getElementById(40003)))
    text->setMessage(langText(str("cgeneralinfotextname")));
  delete ai;
  this->setPanelOpen(33, true);
}

// The plaque and its button kept with what's held: the button enabled only
// while a staff member is held (setPlaceItem 0x4f189a: staffInfoPlaque
// 0x4f188a, slot 0x68 with a thing, 0x6c without); the plaque shut by its
// own close (splaque:CloseWindow, staffPlaque_close 0x4816a9: 0x63cf9c
// cleared) or with Hire Staff (personel_Staff_hide 0x4e6794: 0x481648(0)
// while the button's on; personel_Panel_hide 0x4e6776 lets the HUD's
// button 0x3ed go) lets the button go (splaque_Panel_hide 0x48168c, slot
// 0x54); shown, it's on (splaque_Panel_show 0x4816b2, slot 0x50)
void UiGameScreen::refreshStaffPlaque() {
  if (!this->staffPlaqueFile.empty() && this->isPanelOpen(33) && !this->isPanelOpen(10))
    this->showStaffPlaque("");
  if (!this->isPanelOpen(33))
    this->staffPlaqueFile.clear();
  for (BuyPanel &p : this->buyPanels)
    if (p.id == 10 && p.info)
      if (UiButton *plaque = dynamic_cast<UiButton *>(p.layout->getElementById(p.info))) {
        if (!this->isPanelOpen(33))
          plaque->setToggledOn(false);
        plaque->setDisabled(this->staffToolFile().empty());
      }
}

// Adopt Animals' info plaque (plaque.lyt, 27): zoo.exe 0x483157 - the
// animal type kept (0x64318c; the same one again isn't filled again) and
// filled by 0x4834cc: 25001 its name (slot 0x20, setTextId slot 0xc8),
// 25002 its cPlaqueImageName (+0x450), 25050 its continent's picture
// (cLocation +0x1f4, 0x4f85d2: ui/infoimg.cfg), 25055 its era's name
// (cEra +0x1f8; none: hidden and switched off, flag 0x10), the show star
// and its words (25098, 25099) shown only for an animal with tricks (+0x6f,
// loadConfig 0x4b6c51: its trick list not empty) other than the jellyfish
// (5403), 25051 its habitat's picture (cHabitat +0x1f0), 25053 its
// cPrefIcon (+0x468), then 25003 the lang DLLs' TEXT resource its
// cGeneralInfoTextName names (+0x45c; 0x4834b5 -> 0x483481 -> 0x4f0264);
// 0x4830c0: the carer's button (25011) pictured for its cKeeper (9500 the
// zookeeper, 9551 the scientist, 9552 the marine specialist) and greyed,
// the plaque showing the animal held (slot 0x6c). Shown (0x483463, slot
// 0x4c), the carer's Recommendations shut (plaque_Panel_show 0x4e2865 ->
// ui_keprinfo_4440f1: uiFind 0x1c, slot 0x48); none: hidden (0x4440d6).
// (The icons' help ids, 0x443bfc, aren't kept: ours show none, as the
// Adopt panel's own)
void UiGameScreen::showAdoptPlaque(const std::string &file) {
  if (file == this->adoptPlaqueFile && (file.empty() || this->isPanelOpen(27)))
    return;
  this->adoptPlaqueFile = file;
  UiLayout *z = nullptr;
  for (Entry &e : this->entries)
    if (e.panel && e.id == 27)
      z = e.layout;
  if (file.empty() || !z) {
    this->setPanelOpen(27, false);
    return;
  }
  IniReader *ai = this->resource_manager->getIniReader(file);
  // (per subtype: the sex chosen's, else the male's, else the species'
  // default's - the female sections mostly have none - else the shared one)
  bool female = false;
  this->animalToolFile(female);
  const std::string def = ai ? Utils::string_to_lower(ai->get("global", "defaultsubtype")) : "";
  std::vector<std::string> subs = {female ? "f/" : "m/", "m/"};
  if (!def.empty())
    subs.push_back(def + "/");
  subs.push_back("");
  auto str = [&](const char *key) {
    for (const std::string &s : subs) {
      std::string v = ai ? ai->get(s + "characteristics/strings", key) : "";
      if (!v.empty())
        return v;
    }
    return std::string();
  };
  auto num = [&](const char *key) {
    for (const std::string &s : subs) {
      int v = ai ? ai->getInt(s + "characteristics/integers", key, 0) : 0;
      if (v)
        return v;
    }
    return 0;
  };
  auto info = [&](int id) {
    auto it = this->infoImages.find(id);
    return it == this->infoImages.end() ? std::string() : it->second;
  };
  auto image = [&](int id, const std::string &path) {
    if (UiImage *img = dynamic_cast<UiImage *>(z->getElementById(id))) {
      img->setImage(path);
      img->setHidden(path.empty());
    }
  };
  const int nameId = num("cnameid");
  if (UiText *name = dynamic_cast<UiText *>(z->getElementById(25001)))
    name->setText(nameId ? this->resource_manager->getString(nameId) : "");
  image(25002, Utils::string_to_lower(str("cplaqueimagename")));
  image(25050, info(num("clocation")));
  const int era = num("cera");
  if (UiText *t = dynamic_cast<UiText *>(z->getElementById(25055))) {
    t->setText(era ? this->resource_manager->getString(era) : "");
    t->setHidden(!era);
    t->setDisabled(!era);
  }
  // (its tricks: the sex's list, else the default subtype's, as Shows::tricks)
  bool tricks = false;
  if (ai) {
    std::string base = "m/tricks";
    if (ai->getList(base, "trick").empty())
      base = (def.empty() ? std::string("m") : def) + "/tricks";
    tricks = !ai->getList(base, "trick").empty();
  }
  const bool star = tricks && nameId != 5403;
  for (int id : {25098, 25099})
    if (UiElement *e = z->getElementById(id)) {
      e->setHidden(!star);
      e->setDisabled(!star);
    }
  image(25051, info(num("chabitat")));
  image(25053, Utils::string_to_lower(str("cpreficon")));
  if (UiText *text = dynamic_cast<UiText *>(z->getElementById(25003)))
    text->setMessage(langText(str("cgeneralinfotextname")));
  if (UiButton *carer = dynamic_cast<UiButton *>(z->getElementById(25011))) {
    const int k = num("ckeeper");
    const int index = k == 9551 ? 1 : k == 9552 ? 2 : 0;
    carer->chooseAnimation(static_cast<size_t>(index));
    carer->chooseHelp(static_cast<size_t>(index));
    carer->setDisabled(true);
  }
  delete ai;
  this->setPanelOpen(28, false);
  this->setPanelOpen(27, true);
}

// The plaque and its button kept with what's held: the button enabled only
// while something is held (setPlaceItem 0x4f189a: adoptInfoPlaque 0x4f187a,
// slot 0x68 with a thing, 0x6c without); the plaque hidden when the animal
// list goes - Adopt shut (buya_Animals_hide 0x4e2ba6) or another tab
// (adoptTabs_off 0x4e3867), both adoptSelectionUpdate 0x4e2bec(0), 0x4440d6
// while the button's on - and, hidden by its own close (CloseWindow) too,
// the button let go (plaque_Panel_hide 0x4e284a, slot 0x54; 0x63cd60
// cleared); shown, it's on (plaque_Panel_show 0x4e2865, slot 0x50)
void UiGameScreen::refreshAdoptPlaque() {
  bool female = false;
  const std::string held = this->animalToolFile(female);
  if (!this->adoptPlaqueFile.empty() && this->isPanelOpen(27) && held.empty() &&
      (!this->isPanelOpen(3) || [&] {
        for (const BuyPanel &p : this->buyPanels)
          if (p.id == 3)
            return p.category != "animals";
        return true;
      }()))
    this->showAdoptPlaque("");
  if (!this->isPanelOpen(27))
    this->adoptPlaqueFile.clear();
  for (BuyPanel &p : this->buyPanels)
    if (p.id == 3 && p.info)
      if (UiButton *plaque = dynamic_cast<UiButton *>(p.layout->getElementById(p.info))) {
        plaque->setToggledOn(this->isPanelOpen(27));
        plaque->setDisabled(held.empty());
      }
}

// zoo.exe personel_Staff_off 0x4e6711: Hire Staff's pick let go - the cost
// and duties emptied (staffPanelClearText 0x4e664a), the labels and name
// hidden (0x4e66bf), the rotate arrows switched off and hidden (0x4e65ab),
// nothing held (setPlaceItem 0x4f189a with none)
void UiGameScreen::hireStaffLetGo(BuyPanel &p) {
  if (!p.region || p.region->getSelected() < 0)
    return;
  p.region->setSelected(-1);
  this->showItem(p, -1);
}

bool UiGameScreen::gateMode() const {
  if (!this->isPanelOpen(8))
    return false;
  for (const BuyPanel &p : this->buyPanels)
    if (p.id == 8 && p.category == "fence" && p.gate)
      if (UiButton *b = dynamic_cast<UiButton *>(p.layout->getElementById(p.gate)))
        return b->isToggledOn() && !b->isDisabled();
  return false;
}

// The Build Exhibit panel's gate button (zoo.exe buldhab_GateButton_click
// 0x47b9f3 -> 0x4e1064): gate mode on puts the bulldozer away
// (bulldozeToolOff 0x443b60) and lets the held thing go (setPlaceItem
// 0x4f189a with none)
void UiGameScreen::setGateMode(bool on) {
  if (on)
    this->bulldozerOff();
  for (BuyPanel &p : this->buyPanels)
    if (p.id == 8 && p.gate)
      if (UiButton *b = dynamic_cast<UiButton *>(p.layout->getElementById(p.gate))) {
        b->setToggledOn(on);
        // (the fence picked is let go, as the original)
        if (on && p.region && p.category == "fence") {
          p.region->setSelected(-1);
          this->showItem(p, -1);
          b->setToggledOn(true);
        }
      }
}

// ----------------------------------------------------------------------------
// Terraform (buldhab.lyt's last two tabs, both showing ui/teraform.lyt)
// ----------------------------------------------------------------------------
// Measured against the original: Terrain Types lists every terrain of
// terrain/tiletex*.cfg by type (those without an icon, the waterfall and
// trampled ground, left out), grass picked; below, "Modification Cost $0"
// and the terrain's name and its price for the brush (cost per tile x the
// brush's tiles: Grass $1000 at 5x5). Terrain Height shows the four height
// tools instead (the first chosen) and the chosen tool's help text. The
// brush starts at its largest, 5x5 (+ greyed); undo and accept are greyed
// until something is changed.
// (zoo.exe teraform_Panel_show 0x4da60d fills the page: the terrains, the
// brush's size 0x4da53a and its buttons 0x4da59e, the first tab; the click
// on a terrain, teraform_Objects_click 0x4dc9d2: terraform mode on, the
// terrain's type handed to the zoo view, 0x4dc9bf, and its name shown, 0xcf7)
void UiGameScreen::setupTerraform() {
  TerraformPage &t = this->terraform;
  for (Entry &e : this->entries)
    if (e.panel && e.id == 8)
      t.panel = e.layout;
  if (!t.panel)
    return;
  t.typesTab = dynamic_cast<UiButton *>(t.panel->getElementById(3362));
  t.heightTab = dynamic_cast<UiButton *>(t.panel->getElementById(3361));
  t.region = dynamic_cast<UiScrollingRegion *>(t.panel->getElementById(3350));
  t.brush = dynamic_cast<UiImage *>(t.panel->getElementById(3320));
  t.plus = dynamic_cast<UiButton *>(t.panel->getElementById(3310));
  t.minus = dynamic_cast<UiButton *>(t.panel->getElementById(3314));
  if (t.brush)
    t.brushes = t.brush->getImageSet();
  // The layout hides + and - (state=1); the page shows them
  for (UiButton *b : {t.plus, t.minus})
    if (b)
      b->setHidden(false);
  // The first height tool (hills and valleys) starts chosen
  if (UiButton *b = dynamic_cast<UiButton *>(t.panel->getElementById(3315)))
    b->choose();
  // (the original opens on the 2 x 2 brush)
  t.brushIndex = t.brushes.size() > 1 ? 1 : 0;
  // (Save 3302 accepts the changes, zoo.exe teraform_SaveButton_click
  // 0x489253 -> 0x4da19c; Cancel 3303 undoes them, 0x618a22 -> 0x6097ef;
  // + and - a brush size up or down, 0x4e5ddd / 0x4e5e04, then
  // terraformSizeButtons 0x4da59e greys the end one)
  if (UiButton *b = dynamic_cast<UiButton *>(t.panel->getElementById(3302)))
    b->onClick = [this] {
      if (this->terraformCommand)
        this->terraformCommand(true);
    };
  if (UiButton *b = dynamic_cast<UiButton *>(t.panel->getElementById(3303)))
    b->onClick = [this] {
      if (this->terraformCommand)
        this->terraformCommand(false);
    };
  if (t.plus)
    t.plus->onClick = [this] {
      if (this->terraform.brushIndex + 1 < (int)this->terraform.brushes.size())
        this->terraform.brushIndex++;
    };
  if (t.minus)
    t.minus->onClick = [this] {
      if (this->terraform.brushIndex > 0)
        this->terraform.brushIndex--;
    };

  // The terrains, by type (a later file's entry for a type wins)
  std::map<int, Terrain> byType;
  for (const std::string &cfg :
       this->resource_manager->listResources("terrain/tiletex", ".cfg")) {
    IniReader *ini = this->resource_manager->getIniReader(cfg);
    if (!ini)
      continue;
    for (const std::string &section : ini->getSections()) {
      Terrain terrain;
      terrain.type = ini->getInt(section, "type", -1);
      terrain.icon = ini->get(section, "icon");
      terrain.cost = ini->getInt(section, "cost", 0);
      int helpId = ini->getInt(section, "helpid", 0);
      if (terrain.type < 0 || terrain.icon.empty() ||
          !this->resource_manager->hasResource(terrain.icon + ".ani"))
        continue;
      terrain.name = helpId ? this->resource_manager->getString(helpId) : "";
      byType[terrain.type] = terrain;
    }
    delete ini;
  }
  for (auto &kv : byType)
    t.terrains.push_back(kv.second);

  if (t.region) {
    std::vector<UiScrollingRegion::Item> cells;
    for (const Terrain &terrain : t.terrains)
      cells.push_back({terrain.icon, terrain.name});
    t.region->setItems(cells);
    t.region->setSelected(t.terrains.empty() ? -1 : 0);
    t.region->onSelect = [this](int index) { this->terraform.selected = index; };
  }
  this->refreshTerraform();
}

UiGameScreen::TerraformState UiGameScreen::terraformState() const {
  TerraformState s;
  const TerraformPage &t = this->terraform;
  if (!t.panel || !this->isPanelOpen(8))
    return s;
  bool types = t.typesTab && t.typesTab->isToggledOn();
  bool height = t.heightTab && t.heightTab->isToggledOn();
  s.active = types || height;
  s.painting = types;
  if (t.selected >= 0 && t.selected < (int)t.terrains.size())
    s.type = t.terrains[t.selected].type;
  s.size = t.brushIndex + 1;
  // (the height tools, zoo.exe teraform_modecontig_click 0x4dd643 - hills
  // and valleys, text 3315; modenoncontig 0x4dd44e - cliffs and moats,
  // 3316; modecontig_stamp 0x48e4c4 - level hills, 3317; and
  // modenoncontig_stamp 0x5ac48d - level cliffs, 3318: each sets its help
  // line 3319 to its own text and the view's stamp and contiguous modes)
  const int modes[4] = {3315, 3316, 3317, 3318};
  for (int i = 0; i < 4; i++)
    if (UiButton *b = dynamic_cast<UiButton *>(t.panel->getElementById(modes[i])))
      if (b->isToggledOn())
        s.mode = i;
  return s;
}

// Modification Cost: the total so far; Accept while there's something the
// zoo can pay for, Undo while there's anything
void UiGameScreen::setTerraformCost(float cost) {
  TerraformPage &t = this->terraform;
  if (!t.panel)
    return;
  if (UiText *e = dynamic_cast<UiText *>(t.panel->getElementById(3305)))
    e->setText("$" + std::to_string(static_cast<int>(std::fabs(cost))));
  if (UiElement *e = t.panel->getElementById(3302))
    e->setDisabled(cost <= 0);
  if (UiElement *e = t.panel->getElementById(3303))
    e->setDisabled(cost == 0);
}

void UiGameScreen::refreshTerraform() {
  TerraformPage &t = this->terraform;
  if (!t.panel)
    return;
  const bool types = t.typesTab && t.typesTab->isToggledOn();
  const bool height = t.heightTab && t.heightTab->isToggledOn();
  const bool shown = types || height;

  // The page covers the panel's own grid and details; they keep their
  // state for when a buy tab is picked again
  static const int covered[] = {3261, 3204, 3240, 3208, 3222, 3210, 3258, 3259,
                                3263, 3207, 3284, 3283, 3281, 3280, 3296, 3299};
  if (shown != t.shown) {
    for (int id : covered) {
      UiElement *e = t.panel->getElementById(id);
      if (!e)
        continue;
      if (shown) {
        t.covered[id] = e->isHidden();
        e->setHidden(true);
      } else {
        e->setHidden(t.covered[id]);
      }
    }
    t.shown = shown;
    // Back on a buy tab: its own details (the tab may have changed while
    // the page was up)
    if (!shown)
      for (BuyPanel &p : this->buyPanels)
        if (p.layout == t.panel && p.region) {
          int i = p.region->getSelected();
          this->showDetails(p, i >= 0 && i < (int)p.items.size() ? p.items[i] : nullptr);
        }
  }
  if (!shown)
    return;

  auto element = [&](int id) { return t.panel->getElementById(id); };
  auto text = [&](int id, const std::string &s) {
    if (UiText *e = dynamic_cast<UiText *>(element(id)))
      e->setText(s);
  };
  auto show = [&](int id, bool on) {
    if (UiElement *e = element(id))
      e->setHidden(!on);
  };

  text(3306, this->resource_manager->getString(types ? 3362 : 3361));
  show(3350, types);
  show(3355, types);
  for (int id : {3315, 3316, 3317, 3318})
    show(id, height);
  show(3304, types);
  show(3321, types);

  int n = t.brushIndex + 1; // brushN covers N x N tiles
  if (t.brush && t.brushIndex < (int)t.brushes.size())
    t.brush->setImage(t.brushes[t.brushIndex]);
  if (t.plus)
    t.plus->setDisabled(t.brushIndex + 1 >= (int)t.brushes.size());
  if (t.minus)
    t.minus->setDisabled(t.brushIndex <= 0);

  if (types) {
    const Terrain *terrain =
        t.selected >= 0 && t.selected < (int)t.terrains.size()
            ? &t.terrains[t.selected]
            : nullptr;
    text(3319, terrain ? terrain->name : "");
    // No thousands separator here, unlike the buy panels
    text(3321, terrain ? "$" + std::to_string(terrain->cost * n * n) : "");
  } else {
    // The chosen tool's help text (the tools' ids are their strings)
    int mode = 3315;
    for (int id : {3315, 3316, 3317, 3318})
      if (UiButton *b = dynamic_cast<UiButton *>(element(id)))
        if (b->isToggledOn())
          mode = id;
    text(3319, this->resource_manager->getString(mode));
    text(3321, "");
  }
}

// ----------------------------------------------------------------------------
// Research (research.lyt, id 15)
// ----------------------------------------------------------------------------
void UiGameScreen::setupResearch() {
  ResearchPanel &r = this->research;
  for (Entry &e : this->entries)
    if (e.panel && e.id == 15)
      r.panel = e.layout;
  if (!r.panel)
    return;
  Research::get().load(this->resource_manager);
  // (its tabs: zoo.exe research_Research_click 0x470a71 and
  // research_Conservation_click 0x47104b only refresh the page, 0x4eb270
  // and 0x4eae69)
  r.researchTab = dynamic_cast<UiButton *>(r.panel->getElementById(4009));
  r.conservationTab = dynamic_cast<UiButton *>(r.panel->getElementById(4010));

  // The content filter starts on "All", as in the buy panels
  if (UiButton *b = dynamic_cast<UiButton *>(r.panel->getElementById(4021)))
    b->setLabel(this->resource_manager->getString(22900));
  // Funding - and + (hidden in the layout until the page shows them): a
  // step through the branch's levels, kept to them (zoo.exe
  // resrch2_Buttons_click 0x46c2b9: Less, 4003, 0x61a547; More
  // researchFundingStep 0x46c2e0: past the last, the last)
  auto funding = [this](int step) {
    ResearchBranch *b = Research::get().branch(this->research.shownBranch);
    if (!b || b->funding.empty())
      return;
    b->fundingLevel =
        std::clamp(b->fundingLevel + step, 0, (int)b->funding.size() - 1);
    this->chargeFunding();
  };
  if (UiButton *less = dynamic_cast<UiButton *>(r.panel->getElementById(4003))) {
    less->setHidden(false);
    less->onClick = [funding] { funding(-1); };
  }
  if (UiButton *more = dynamic_cast<UiButton *>(r.panel->getElementById(4004))) {
    more->setHidden(false);
    more->onClick = [funding] { funding(1); };
  }
  this->refreshResearch();
}

// (run after every click and tab change here - as the original's tab
// clicks run its refresh: research_Status_click 0x4a02f7 -> 0x4eae69)
void UiGameScreen::refreshResearch() {
  ResearchPanel &r = this->research;
  if (!r.panel)
    return;
  Research &research = Research::get();
  auto element = [&](int id) { return r.panel->getElementById(id); };
  auto text = [&](int id, const std::string &s) {
    if (UiText *e = dynamic_cast<UiText *>(element(id)))
      e->setText(s);
  };
  auto image = [&](int id, const std::string &path) {
    if (UiImage *e = dynamic_cast<UiImage *>(element(id))) {
      e->setImage(path);
      e->setHidden(path.empty());
    }
  };
  auto progress = [&](int id, const ResearchProgram *p) {
    if (UiStatusImage *e = dynamic_cast<UiStatusImage *>(element(id)))
      e->setValue(p && p->cost > 0 ? p->progress * 100 / p->cost : 0);
  };

  // Status: each branch's program (Research, then Conservation)
  const int statusIds[2][3] = {{4016, 4019, 4018}, {4012, 4020, 4014}};
  for (int i = 0; i < 2; i++) {
    ResearchBranch *b = research.branch(i);
    ResearchProgram *p = b ? research.current(*b) : nullptr;
    text(statusIds[i][0], p ? p->name : "");
    image(statusIds[i][1], p ? p->icon : b ? b->noProgramIcon : "");
    progress(statusIds[i][2], p);
  }

  // The category page: the branch of the chosen tab (zoo.exe 0x4eb21b: the
  // TabRadioSet's choice - Program Status none, the next tab the first
  // branch, the one after the second; shown again whenever the panel is,
  // 0x41a3ba)
  int shown = r.researchTab && r.researchTab->isToggledOn()           ? 0
              : r.conservationTab && r.conservationTab->isToggledOn() ? 1
                                                                      : -1;
  ResearchBranch *b = research.branch(shown);
  UiListBox *list = dynamic_cast<UiListBox *>(element(4022));
  int filterNow = this->filterChoice.count(15) ? this->filterChoice[15] : 0;
  if (shown != r.shownBranch || filterNow != r.shownFilter) {
    r.shownBranch = shown;
    r.shownFilter = filterNow;
    r.listed.clear();
    if (list && b) {
      list->clear();
      for (int i = 0; i < (int)b->categories.size(); i++) {
        if (!this->passesFilter(15, b->categories[i].expansion))
          continue;
        list->addItem(b->categories[i].name, b->categories[i].file);
        list->setChecked((int)r.listed.size(), b->enabled[i]);
        r.listed.push_back(i);
      }
    }
  }
  // The panel's title is the page's ("Program Status", "Research", ...)
  for (int tab : {4008, 4009, 4010})
    if (UiButton *t = dynamic_cast<UiButton *>(element(tab)))
      if (t->isToggledOn())
        text(4001, this->resource_manager->getString(t->getHelpId()));
  if (!b)
    return;
  // The checkboxes say which categories are researched (resrch2_list_click
  // zoo.exe 0x6173f3: the row's category flag = its checkbox, nothing else).
  // Unchecking the category being worked on doesn't pick another here: the
  // research tick does that (Research::advance, 0x41f1ba)
  if (list) {
    for (int row = 0; row < (int)r.listed.size(); row++) {
      int i = r.listed[row];
      if (list->isChecked(row) != b->enabled[i])
        b->enabled[i] = list->isChecked(row);
    }
  }

  if (!b->funding.empty()) {
    const ResearchFunding &f = b->funding[b->fundingLevel];
    // ("%s normal" with the cost a day, zoo.exe 0x470d0f: the level's cost a
    // month - its file's cost, which researchTick 0x41f1ba pays over 43200
    // minutes - times 1/30 (0x635040), cut toward zero, through moneyToText
    // 0x40eca1 without grouping: $33 for 1000)
    std::string name = f.name;
    size_t at = name.find("%s");
    if (at != std::string::npos)
      name.replace(at, 2, formatCost(static_cast<int>(static_cast<float>(f.cost) * Original::fundingShownPerDay)));
    text(4006, name);
    if (UiElement *less = element(4003))
      less->setDisabled(b->fundingLevel <= 0);
    if (UiElement *more = element(4004))
      more->setDisabled(b->fundingLevel + 1 >= (int)b->funding.size());
  }

  // The program being researched and its category
  int c = b->currentCategory;
  ResearchCategory *category =
      c >= 0 && c < (int)b->categories.size() ? &b->categories[c] : nullptr;
  ResearchProgram *p = research.current(*b);
  text(4024, category ? category->name : "");
  text(4025, p ? p->name : "");
  image(4029, p ? p->icon : b->noProgramIcon);
  progress(4027, p);
}

// ----------------------------------------------------------------------------
// Zoo Status (zooinfo.lyt, id 14), measured against the original's freeform
// Death Mountain: Zoo Information shows the zoo's name, admission ($22.00,
// children half rounded to the cent; the arrows change it by $0.25), the
// counts and marketing funding (mktg.cfg's levels, "$0 none" to start, -
// greyed at none); Income / Expenses shows this month's column (incomes
// green, costs red, $0 in a new zoo), total cash, net income and the zoo's
// value (cash plus the purchase cost of everything on the map); the graphs
// start with this month's value; the commerce list's header for the chosen
// sort is gold.
// ----------------------------------------------------------------------------
static std::string formatCents(long cents) {
  char buffer[32];
  snprintf(buffer, sizeof(buffer), "$%ld.%02ld", cents / 100, cents % 100);
  return buffer;
}

// ----------------------------------------------------------------------------
// The scenario page (zooinfo4.lyt, id 803: the Scenario information button
// 4107, zooinfo4_Panel_show 0x4efdf6): ScenarioName 4142, Story 4143 (its
// scroll bar 4144) and the goals list Scenario_data 4145 (bar 4146, not
// selectable), measured against the original's Seasideville Zoo
// ----------------------------------------------------------------------------
void UiGameScreen::setupScenarioPage() {
  for (Entry &e : this->entries) {
    if (e.panel && e.id == 803)
      this->scenarioPanel = e.layout;
    if (e.panel && e.id == 45400)
      this->objectivePanel = e.layout;
  }
  if (this->objectivePanel)
    if (UiButton *bar = dynamic_cast<UiButton *>(this->objectivePanel->getElementById(45406)))
      bar->onClick = [this] { this->clickObjective(); };
  UiLayout *s = this->scenarioPanel;
  if (!s)
    return;
  // (a zoo loaded empties the goals list, 0x4c740a: ours fills it as the
  // page opens)
  // The story scrolls by the layout's own bar; each goal's row wraps beside
  // its icon (miniconwidth, miniconheight 32)
  if (UiText *story = dynamic_cast<UiText *>(s->getElementById(4143)))
    if (UiScrollBar *bar = dynamic_cast<UiScrollBar *>(s->getElementById(4144)))
      story->linkScrollBar(bar);
  if (UiListBox *list = dynamic_cast<UiListBox *>(s->getElementById(4145)))
    list->setWrapped(true);
}

void UiGameScreen::setObjective(const std::string &text) {
  this->objective = text;
  if (this->objectivePanel)
    if (UiButton *bar = dynamic_cast<UiButton *>(this->objectivePanel->getElementById(45406)))
      bar->setLabel(text);
  this->setPanelOpen(45400, !text.empty());
}

// objectve_Message_click 0x4a09f8
void UiGameScreen::clickObjective() {
  if (this->onObjectiveClick)
    this->onObjectiveClick();
}

// zoo.exe 0x4f004a: the name (0x4fc83c) and the story (0x4f0497 through
// 0x4f0264) set, list 4145 cleared and each goal's line added (0x4f042c)
void UiGameScreen::refreshScenarioPage() {
  UiLayout *s = this->scenarioPanel;
  if (!s || !this->scenarioPage)
    return;
  const ScenarioPage page = this->scenarioPage();
  if (UiText *name = dynamic_cast<UiText *>(s->getElementById(4142)))
    name->setText(page.name);
  if (UiText *story = dynamic_cast<UiText *>(s->getElementById(4143)))
    story->setText(page.story);
  if (UiListBox *list = dynamic_cast<UiListBox *>(s->getElementById(4145))) {
    list->clear();
    for (const auto &line : page.lines)
      list->addItem(line.first, "", line.second);
  }
}

void UiGameScreen::setupZooStatus() {
  for (Entry &e : this->entries)
    if (e.panel && e.id == 14)
      this->zooPanel = e.layout;
  if (!this->zooPanel)
    return;
  UiLayout *z = this->zooPanel;

  // Marketing levels: mktg.cfg names the level file (mktgnorm.cfg) - zoo.exe
  // marketingLoad 0x5915e3 reads its [marketing], marketingLoadConfig
  // 0x5916ff each level
  if (IniReader *m = this->resource_manager->getIniReader("mktg.cfg")) {
    std::string file = m->get("marketing", "marketing");
    delete m;
    if (IniReader *levels = file.empty() ? nullptr
                                         : this->resource_manager->getIniReader(file)) {
      for (const std::string &key : levels->getList("marketing", "funding")) {
        int nameId = levels->getInt(key, "name", 0);
        this->marketing.push_back(
            {nameId ? this->resource_manager->getString(nameId) : key,
             static_cast<float>(std::atof(levels->get(key, "cost", "0").c_str())), levels->getInt(key, "benefit", 0)});
      }
      delete levels;
    }
  }
  auto button = [&](int id) { return dynamic_cast<UiButton *>(z->getElementById(id)); };
  if (UiButton *less = button(4052))
    less->onClick = [this] {
      if (this->marketingLevel > 0)
        this->marketingLevel--;
      this->chargeFunding();
    };
  if (UiButton *more = button(4053))
    more->onClick = [this] {
      if (this->marketingLevel + 1 < (int)this->marketing.size())
        this->marketingLevel++;
      this->chargeFunding();
    };
  // Admission: $0.25 a click (the price ZTGameMgr::admissionPrice 0x4f6db4
  // gives, +0x1150; zoo.exe zooinfo1_UpSpinner 0x616e56,
  // DownSpinner 0x616e85), kept to economy.cfg's limits (setAdmissionPrice
  // 0x4bbf10: $0 to $100 in the shipped game)
  if (UiButton *up = button(4195))
    up->onClick = [this] {
      this->zoo.admission = Original::clampAdmission(static_cast<float>(this->zoo.admission + Original::priceStep));
    };
  if (UiButton *down = button(4191))
    down->onClick = [this] {
      this->zoo.admission = Original::clampAdmission(static_cast<float>(this->zoo.admission - Original::priceStep));
    };
  // The zoo's name (the layout hides it; a freeform zoo shows it), and the
  // admission price, both typed into
  for (int id : {4198, 4199})
    if (UiElement *e = z->getElementById(id))
      e->setHidden(false);
  if (UiText *name = dynamic_cast<UiText *>(z->getElementById(4199)))
    name->onCommit = [this](const std::string &typed) {
      if (!typed.empty())
        this->zoo.name = typed;
    };
  if (UiText *price = dynamic_cast<UiText *>(z->getElementById(4190))) {
    price->onBeginEdit = [this] {
      char buffer[32];
      snprintf(buffer, sizeof(buffer), "%.2f", this->zoo.admission);
      return std::string(buffer);
    };
    price->onCommit = [this](const std::string &typed) {
      if (typed.empty())
        return;
      // As typed, kept to the limits (zoo.exe
      // zooinfo1_AdmissionsPriceCost_click 0x616eb4: read with 0x4b40ae,
      // set with setAdmissionPrice 0x4bbf10 - not rounded)
      this->zoo.admission = Original::clampAdmission(static_cast<float>(std::atof(typed.c_str())));
    };
  }
}

void UiGameScreen::setZooInfo(const ZooInfo &info) {
  this->zoo = info;
  // (open, it waits for its next refresh, as the original's - 0x401644)
  if (!this->isPanelOpen(14))
    this->refreshZooStatus();
}

// zoo.exe 0x401644 (each frame from 0x41a281, not while paused -
// mouseEdgeScroll 0x419eb2): Zoo Status (uiFind 14) shown and not hidden,
// its counts, books, graphs and pages redone when the time (ms) reaches
// +0x643670, which then moves 10 s on
void UiGameScreen::zooStatusTick(uint64_t nowMs, bool paused) {
  if (paused || !this->isPanelOpen(14) || nowMs < this->zooStatusDue)
    return;
  this->zooStatusDue = nowMs + Original::zooStatusRefreshMs;
  this->refreshZooStatus();
  this->refreshScenarioPage();
}

// zoo.exe 0x41739a: 0x639145 set, then 0x401644(0) - redone at once, and
// again on the next tick (the due time becomes 0 + 10 s)
void UiGameScreen::forceZooStatus() {
  if (!this->isPanelOpen(14))
    return;
  this->refreshZooStatus();
  this->refreshScenarioPage();
  this->zooStatusDue = 0;
}

void UiGameScreen::setSim(ZooSim *sim) {
  this->sim = sim;
  this->chargeFunding();
}

// Marketing funding: a level's cost is per month, paid by the tick (see
// ZooSim). (Research and conservation pay theirs from the research tick,
// only while a program's being worked on - Research::advance)
void UiGameScreen::chargeFunding() {
  if (!this->sim)
    return;
  double marketing = this->marketing.empty() ? 0 : this->marketing[this->marketingLevel].cost;
  this->sim->setMonthlyCost(ZooSim::Marketing, 0, marketing);
}

void UiGameScreen::refreshZooStatus() {
  UiLayout *z = this->zooPanel;
  if (!z)
    return;
  auto text = [&](int id, const std::string &s) {
    if (UiText *t = dynamic_cast<UiText *>(z->getElementById(id)))
      if (!t->isBeingEdited())
        t->setText(s);
  };
  const SDL_Color green = {83, 219, 83, 255}, red = {255, 0, 0, 255};
  auto money = [&](int id, long amount) {
    if (UiText *t = dynamic_cast<UiText *>(z->getElementById(id))) {
      t->setText(formatPrice(amount < 0 ? -amount : amount));
      t->setTextColor(amount < 0 ? red : green);
    }
  };

  // Zoo Information
  text(4199, this->zoo.name);
  long adult = std::lround(this->zoo.admission * 100);
  text(4190, formatCents(adult));
  text(4193, formatCents(adult));
  text(4194, formatCents((adult + 1) / 2));
  // The admission's arrows greyed at the ends (zoo.exe 0x530329, Zoo
  // Status' first tab: the up arrow off - its slot 0x6c - once the price
  // is no less than cMaxAdultAdmissionPrice, +0x1158; the down arrow once
  // it's no more than cMinAdultAdmissionPrice, +0x1154; else on, 0x68)
  {
    const Original::Economy &e = Original::economy();
    if (UiButton *up = dynamic_cast<UiButton *>(z->getElementById(4195)))
      up->setDisabled(!(this->zoo.admission < e.maxAdmission));
    if (UiButton *down = dynamic_cast<UiButton *>(z->getElementById(4191)))
      down->setDisabled(this->zoo.admission <= e.minAdmission);
  }
  for (int id : {4115, 4117, 4119, 4121, 4122, 4171})
    text(id, "0");
  text(4115, std::to_string(this->zoo.animalCount));
  text(4117, std::to_string(this->zoo.exhibitCount));
  text(4119, std::to_string(this->zoo.attractionCount));
  text(4121, std::to_string(this->zoo.guestCount));
  text(4122, std::to_string(this->zoo.staffCount));
  text(4171, std::to_string(this->zoo.benefactorCount));
  if (!this->marketing.empty()) {
    this->marketingLevel =
        std::clamp(this->marketingLevel, 0, (int)this->marketing.size() - 1);
    const MarketingLevel &m = this->marketing[this->marketingLevel];
    std::string s = m.name;
    size_t at = s.find("%s");
    if (at != std::string::npos) // (zoo.exe 0x52f586: the level's cost, cut, not grouped)
      s.replace(at, 2, formatCost(static_cast<int>(m.cost)));
    text(4054, s);
    if (UiElement *less = z->getElementById(4052))
      less->setDisabled(this->marketingLevel <= 0);
    if (UiElement *more = z->getElementById(4053))
      more->setDisabled(this->marketingLevel + 1 >= (int)this->marketing.size());
  }

  // Income / Expenses: the last four months, this month on the right (ids
  // +0 this month, +15 three months ago, +30 two, +45 last month)
  const ZooSim::Month blank;
  const std::vector<ZooSim::Month> *history = this->sim ? &this->sim->history() : nullptr;
  auto monthAt = [&](int back) -> const ZooSim::Month * { // 0 = this month
    if (!history || back >= (int)history->size())
      return back == 0 ? &blank : nullptr;
    return &(*history)[history->size() - 1 - back];
  };
  const int columnBack[4] = {0, 3, 2, 1}; // id offset / 15 -> months back
  // Each row's first id, and whether it is money spent
  const struct {
    int id;
    ZooSim::Line line;
  } rows[] = {{3931, ZooSim::AdmissionsIncome}, {3938, ZooSim::Donations},
              {3936, ZooSim::Concessions},      {3942, ZooSim::ShowIncome},
              {3939, ZooSim::Recycling},        {3932, ZooSim::Construction},
              {3933, ZooSim::AnimalPurchase},   {3934, ZooSim::Upkeep},
              {3937, ZooSim::Wages},            {3935, ZooSim::Research},
              {3940, ZooSim::Marketing}};
  auto signedMoney = [&](int id, double amount, bool cost) {
    UiText *t = dynamic_cast<UiText *>(z->getElementById(id));
    if (!t)
      return;
    long v = std::lround(amount);
    t->setText(v < 0 ? "-" + formatPrice(-v) : formatPrice(v));
    t->setTextColor(cost || v < 0 ? red : green);
  };
  for (int c = 0; c < 4; c++) {
    const ZooSim::Month *m = monthAt(columnBack[c]);
    int offset = c * 15;
    text(3920 + (c == 0 ? 0 : c), m ? this->resource_manager->getString(22101 + m->month) : "");
    text(3930 + offset, m ? std::to_string(std::lround(m->lines[ZooSim::Admissions])) : "");
    for (const auto &r : rows) {
      if (!m) {
        text(r.id + offset, "");
        continue;
      }
      bool cost = r.line >= ZooSim::Construction;
      signedMoney(r.id + offset, m->lines[r.line], cost);
    }
    if (m)
      signedMoney(3941 + offset, m->total(), false);
    else
      text(3941 + offset, "");
  }
  double cash = this->sim ? this->sim->cash() : this->zoo.cash;
  double net = this->sim && !this->sim->history().empty()
                   ? this->sim->history().back().total()
                   : 0;
  money(3926, std::lround(cash));                                   // total cash
  signedMoney(3991, net, false);                                    // net income
  money(3928, this->zoo.value >= 0 ? this->zoo.value                // zoo value (0x52fa39: the
                                   : std::lround(cash) + this->zoo.placedValue); // rating's, row 15)

  // The graphs: a point a month, this month's and the 11 before it at most
  // (zoo.exe zooGraphsRefresh 0x52ff4d: graphSeriesA-D 0x52feef, 0x530a0e,
  // 0x530aff, 0x530bf2 each walk 12 months from the month 11 back - the
  // books' month +0x14c - leaving out those before the zoo's first year)
  if (history) {
    std::vector<UiGraph::Point> rating, donations, profit, attendance;
    const size_t first = history->size() > Original::graphMonths ? history->size() - Original::graphMonths : 0;
    for (size_t i = first; i < history->size(); i++) {
      const ZooSim::Month &m = (*history)[i];
      std::string label = this->resource_manager->getString(22101 + m.month);
      rating.push_back({std::floor(m.rating), label});
      donations.push_back({m.lines[ZooSim::Donations], label});
      profit.push_back({m.total(), label});
      // (13965: books row 25, the guests in the zoo - graphSeriesD 0x530bf2,
      // 0x41dd64(25, 0, month) - not the month's admissions, row 4)
      attendance.push_back({m.guests, label});
    }
    auto points = [&](int id, const std::vector<UiGraph::Point> &p) {
      if (UiGraph *g = dynamic_cast<UiGraph *>(z->getElementById(id)))
        g->setPoints(p);
    };
    points(4150, rating);
    points(13978, donations);
    points(13967, profit);
    points(13965, attendance);
  }

  // Graphs: the page's Line / Bar buttons (zoo.exe zooinfo3_Line_click
  // 0x61705e / _Bar_click 0x61707b on 4150, zoofin2 0x617098 / 0x6170b5 on
  // 13978, zoofin3 0x6170d2 on 13967: the graph drawn as lines, 1, or bars,
  // 2 - 0x51a5ef)
  const int graphs[][3] = {{4150, 4151, 4152},
                           {13978, 13991, 13992},
                           {13967, 13994, 13995},
                           {13965, 13997, 13998}};
  for (const auto &g : graphs)
    if (UiGraph *graph = dynamic_cast<UiGraph *>(z->getElementById(g[0])))
      if (UiButton *bar = dynamic_cast<UiButton *>(z->getElementById(g[2])))
        graph->setBars(bar->isToggledOn());

  // Commerce list: the chosen sort's header is gold. A click on the header
  // already chosen turns that column round (zoo.exe buttons104f_click
  // 0x4f09ad, for 0x104f - 0x1053: the column kept in 0x63bbbc, its flag
  // [0x63bbc4 + column] flipped when it's the same one, the list sorted
  // again, 0x4f0979; Zoo Status shown, 0x32a_show 0x5314cc, sorts without
  // the flip); every number column starts biggest first (the flags clear)
  for (int i = 0; i < 5; i++) {
    UiText *label = dynamic_cast<UiText *>(z->getElementById(4184 + i));
    UiButton *sort = dynamic_cast<UiButton *>(z->getElementById(4175 + i));
    if (!label || !sort)
      continue;
    sort->onClick = [this, i] {
      if (this->commerceColumn == i)
        this->commerceUp[i] = !this->commerceUp[i];
      this->commerceColumn = i;
    };
    if (sort->isToggledOn())
      label->setTextColor(SDL_Color{255, 219, 90, 255});
    else
      label->clearTextColor();
  }
  // Completed Research and Conservation (list 4164): each program finished
  // (not those costing nothing), its icon and name
  if (UiListBox *list = dynamic_cast<UiListBox *>(z->getElementById(4164))) {
    std::vector<const ResearchProgram *> done;
    for (ResearchBranch &b : Research::get().branches())
      for (ResearchCategory &c : b.categories)
        for (ResearchProgram &p : c.programs)
          if (p.done && p.cost > 0)
            done.push_back(&p);
    if (static_cast<int>(done.size()) != this->researchListed) {
      list->clear();
      for (const ResearchProgram *p : done)
        list->addItem(p->name, "", p->icon);
      this->researchListed = static_cast<int>(done.size());
    }
  }
  // Zoo Awards (zoo.exe 0x53167f): each award received, its icon
  // (awards/awardN/awardN) and name (29000 + N)
  if (UiListBox *list = dynamic_cast<UiListBox *>(z->getElementById(4124)))
    if (this->awardsListed != static_cast<int>(this->awards.size())) {
      list->clear();
      for (int a : this->awards)
        list->addItem(this->resource_manager->getString(29000 + a), "",
                      "awards/award" + std::to_string(a) + "/award" + std::to_string(a));
      this->awardsListed = static_cast<int>(this->awards.size());
    }
  // Commerce Building List (measured against the original's: "Snack
  // Machine 1  1  2  -$5.50  -$5.50"): every stand and shop, its months
  // open, visitors, profit (takings less upkeep, red under nothing) and
  // profit a month, in the chosen order; picking one centres the view on
  // it and opens its Building Information (zoo.exe 0x104c_click 0x61728e:
  // unless it's the one selected already, the view to its tile, 0x4017e5,
  // selectAndShowInfo 0x59b90c, and selected, 0x4ef75b(it, 1))
  if (UiListBox *list = dynamic_cast<UiListBox *>(z->getElementById(4172))) {
    using Row = CommerceRow;
    std::vector<Row> rows;
    if (this->objects)
      for (const PlacedObjects::Object &o : this->objects->objects()) {
        if (o.label.empty() || o.price < 0)
          continue;
        int months = buildingMonthsOpen(this->sim, this->fences, this->monthNumber, o);
        double profit = o.income - o.upkeep;
        rows.push_back({o.id, o.label, o.typeName, months, o.visitorsTotal, profit, profit / months});
      }
    int sortBy = 0;
    for (int i = 0; i < 5; i++)
      if (UiButton *b = dynamic_cast<UiButton *>(z->getElementById(4175 + i)))
        if (b->isToggledOn())
          sortBy = i;
    this->commerceColumn = sortBy;
    std::stable_sort(rows.begin(), rows.end(), [this, sortBy](const Row &a, const Row &b) {
      return commerceFirst(a, b, sortBy, this->commerceUp[sortBy]);
    });
    auto cents = [](double v) {
      char b[32];
      std::snprintf(b, sizeof b, v < 0 ? "-$%.2f" : "$%.2f", std::fabs(v));
      return std::string(b);
    };
    std::string shown;
    std::vector<int> ids;
    for (const Row &r : rows) {
      shown += r.name + "|" + std::to_string(r.months) + "|" + std::to_string(r.visitors) + "|" + cents(r.profit) + "\n";
      ids.push_back(r.id);
    }
    if (shown != this->commerceShown) {
      int row = list->getScrollPosition();
      list->clear();
      const SDL_Color none = {0, 0, 0, 0}, loss = {255, 0, 0, 255};
      for (const Row &r : rows)
        list->addRow({r.name, std::to_string(r.months), std::to_string(r.visitors), cents(r.profit), cents(r.average)},
                     {none, none, none, r.profit < 0 ? loss : none, r.average < 0 ? loss : none});
      this->commerceShown = shown;
      this->listedCommerce = ids;
      list->setSelectedIndex(-1);
      for (size_t i = 0; i < ids.size(); i++)
        if (ids[i] == this->commercePicked)
          list->setSelectedIndex(static_cast<int>(i));
      list->setScrollPosition(row);
    }
    int sel = list->getSelectedIndex();
    int id = sel >= 0 && sel < static_cast<int>(this->listedCommerce.size()) ? this->listedCommerce[sel] : -1;
    if (id >= 0 && id != this->commercePicked) {
      this->commercePicked = id;
      if (const PlacedObjects::Object *o = this->objects->byId(id)) {
        if (this->centreOn)
          this->centreOn(o->x, o->y);
        this->showBuilding(id);
        // (Zoo Status stays open beside it, as the original)
        this->setPanelOpen(14, true);
        for (UiButton *b : this->panelButtons)
          if (b->getActionTarget() == 14)
            b->setToggledOn(true);
      }
    }
  }
}

UiGameScreen::~UiGameScreen() {
  if (this->speedArtwork) SDL_DestroyTexture(this->speedArtwork);
  for (SDL_Texture *face : this->speedNativeFaces)
    if (face) SDL_DestroyTexture(face);
  delete this->options;
  for (Entry &e : this->entries)
    delete e.layout;
  delete this->filter.packs;
  // (withModsIni goes with withMods: a UiLayout deletes the reader it was
  // made from - deleted here too, it was freed twice, and the heap broke on
  // the next load or trip to the main menu)
  delete this->filter.withMods;
  delete this->dialog.layout;
}

UiElement *UiGameScreen::getElementById(int id) {
  if (id == 1030 && (!this->dateTime.empty() || Features::fastForward) && this->dateTimeText) return this->dateTimeText.get();
  for (Entry &e : this->entries)
    if (UiElement *found = e.layout->getElementById(id))
      return found;
  return nullptr;
}

bool UiGameScreen::isOverPanel(int x, int y) const {
  SDL_Point p = {x, y};
  if ((!this->dateTime.empty() || Features::fastForward) && SDL_PointInRect(&p, &this->dateTimeWell)) return true;
  if (Features::fastForward && SDL_PointInRect(&p, &this->speedRect)) return true;
  for (const Entry &e : this->entries)
    if (e.panel && e.open && SDL_PointInRect(&p, &e.rect))
      return true;
  return false;
}

bool UiGameScreen::isPanelOpen(int id) const {
  for (const Entry &e : this->entries)
    if (e.panel && e.id == id)
      return e.open;
  return false;
}

void UiGameScreen::showPanel(int id, const std::string &category) {
  for (UiButton *b : this->panelButtons) {
    b->setToggledOn(b->getActionTarget() == id);
    this->setPanelOpen(b->getActionTarget(), b->getActionTarget() == id);
  }
  for (BuyPanel &p : this->buyPanels) {
    if (p.id != id || category.empty())
      continue;
    // As a click on the tab: the rest of its radio set (the terraform tabs
    // too) goes off
    for (UiButton *tab : p.tabs)
      if (Utils::string_to_lower(tab->getStringData()) == category)
        tab->choose();
    if (p.layout)
      p.layout->syncTabs();
    this->refreshBuyPanel(p);
    this->refreshTerraform();
  }
}

// The Game Options panel's settings pages, as the original's (zoo.exe
// 0x518375 reads each setting back into its control when the panel opens;
// each control's handler writes it - [UI] keys in zoo.ini), and the Extras
// tab (an addition, above)
void UiGameScreen::setupGameOptions() {
  auto slider = [this](int id, int value, std::function<void(int)> changed) {
    if (UiScrollBar *s = dynamic_cast<UiScrollBar *>(this->getElementById(id))) {
      s->setValue(value);
      s->onChange = std::move(changed);
    }
  };
  // Main volume (gameopt2_MainVolumeScrollbar_click 0x58b98d): 0 is
  // silence (5000), else (100 - value) x 15 hundredths of a decibel down
  slider(1511, std::clamp(100 - Settings::getInt("UI", "userAttenuation", 0) / 15, 0, 100), [](int v) {
    Settings::setInt("UI", "userAttenuation", v == 0 ? 5000 : (100 - v) * 15);
    Settings::apply();
  });
  // Menu music (gameopt2_MainMenuMusicScrollbar_click 0x47ff4d): its top,
  // menuMusicMaxAttenuation, and 50 more for each step down
  int musicTop = Settings::getInt("UI", "menuMusicMaxAttenuation", -1000);
  slider(1513, std::clamp(100 - (Settings::getInt("UI", "menuMusicAttenuation", -1000) - musicTop) / 50, 0, 100),
         [musicTop](int v) {
           Settings::setInt("UI", "menuMusicAttenuation", musicTop + (100 - v) * 50);
           Settings::apply();
         });
  // Scroll speed (gameopt3_MainScrollSpeedBar_click 0x606800, 0x609abf):
  // mouseScrollX/Y = 8 + value x 1.2 (shown back as x / 1.2, 0x518375)
  slider(1515, std::clamp(static_cast<int>(std::lround(Settings::getInt("UI", "mouseScrollX", 32) * 100.0 / 120.0)), 0, 100),
         [](int v) {
           int step = 8 + static_cast<int>(v * 1.2);
           Settings::setInt("UI", "mouseScrollX", step);
           Settings::setInt("UI", "mouseScrollY", step);
           Settings::apply();
         });
  // Tooltips: the delay (0x51a559: value x 10 + 1) and how long one stays
  // (0x51a59f: value x 100 + 500 ms, 5000 by default). ours: the delay is
  // kept but not yet used - the original reads it from UIManager+0x20, and
  // its scale against our measured 600 ms isn't traced yet
  this->tooltipDelayMs = Settings::getInt("UI", "tooltipDelay", 1);
  slider(1523, std::clamp((this->tooltipDelayMs - 1) / 10, 0, 100), [this](int v) {
    this->tooltipDelayMs = v * 10 + 1;
    Settings::setInt("UI", "tooltipDelay", this->tooltipDelayMs);
  });
  this->tooltipDurationMs = static_cast<Uint32>(std::max(500, Settings::getInt("UI", "tooltipDuration", 3000)));
  slider(1525, std::clamp((static_cast<int>(this->tooltipDurationMs) - 500) / 100, 0, 100), [this](int v) {
    this->tooltipDurationMs = static_cast<Uint32>(v * 100 + 500);
    Settings::setInt("UI", "tooltipDuration", static_cast<int>(this->tooltipDurationMs));
  });
  // Short or long tooltips ([UI] helpType: 0 short, else long: zoo.exe
  // gameopt3_ToolTipShort_click 0x48aece writes 0, gameopt3_ToolTipLong_click
  // 0x606822 writes 1). ours: kept;
  // the long ones' width isn't read yet, so both show the same
  int helpType = Settings::getInt("UI", "helpType", 0);
  for (int id : {1520, 1521})
    if (UiButton *b = dynamic_cast<UiButton *>(this->getElementById(id))) {
      if ((id == 1521) == (helpType != 0))
        b->choose();
      b->onClick = [id] { Settings::setInt("UI", "helpType", id == 1521 ? 1 : 0); };
    }
  // Window or full screen (Window_Mode 1587 and Full_Screen_Mode 1588, one
  // over the other: the one shown is the one to change to) and the window's
  // size (800 x 600, 1024 x 768, 1280 x 1024): [user] fullscreen,
  // screenwidth, screenheight
  auto window = [] {
    SDL_Window *w = SDL_GetKeyboardFocus();
    return w ? w : SDL_GetMouseFocus();
  };
  auto showMode = [this, window] {
    SDL_Window *w = window();
    bool full = w && (SDL_GetWindowFlags(w) & SDL_WINDOW_FULLSCREEN) != 0;
    if (UiElement *e = this->getElementById(1587))
      e->setHidden(!full);
    if (UiElement *e = this->getElementById(1588))
      e->setHidden(full);
  };
  for (int id : {1587, 1588})
    if (UiButton *b = dynamic_cast<UiButton *>(this->getElementById(id)))
      b->onClick = [id, window, showMode] {
        if (Embed::active())
          return;
        if (SDL_Window *w = window())
          Window::setFullscreen(w, id == 1588);
        showMode();
      };
  showMode();
  if (Embed::active())
    for (int id : {1587, 1588})
      if (UiElement *e = this->getElementById(id))
        e->setDisabled(true);
  // (the fixed window sizes are left out: soundVideoPage)

  // Interface Size (an addition; UiOptions.hpp's UiScaleControl, as the
  // Options window's)
  this->panelScale.attach(dynamic_cast<UiScrollBar *>(this->getElementById(kUiScaleBar)),
                          dynamic_cast<UiText *>(this->getElementById(kUiScaleLabel)),
                          dynamic_cast<UiButton *>(this->getElementById(kUiScaleAuto)),
                          dynamic_cast<UiText *>(this->getElementById(kUiScaleNote)), "Interface Size: ", false);

  // Extras (addition): each of the engine's additions on or off, kept in
  // zoo.ini's [Extras]
  if (UiText *title = dynamic_cast<UiText *>(this->getElementById(9040)))
    title->setText("Extras");
  if (UiText *note = dynamic_cast<UiText *>(this->getElementById(9049)))
    note->setText("Off: as the original");
  int i = 0;
  for (Features::Switch *fp : extrasOrder()) {
    Features::Switch &f = *fp;
    if (UiText *label = dynamic_cast<UiText *>(this->getElementById(kExtrasLabelFirst + i)))
      label->setText(extrasLabel(f.name));
    int id = kExtrasFirst + i++;
    if (UiButton *b = dynamic_cast<UiButton *>(this->getElementById(id))) {
      b->setToggledOn(*f.value);
      bool *value = f.value;
      std::string name = f.name;
      b->onClick = [b, value, name] {
        *value = b->isToggledOn();
        Settings::saveExtra(name, *value);
      };
    }
  }
  // (the pages, when there are more switches than rows: the first shown)
  int pages = (i + kExtrasRows - 1) / kExtrasRows;
  auto turnTo = [this, i, pages](int page) {
    this->extrasShownPage = std::clamp(page, 0, std::max(0, pages - 1));
    for (int k = 0; k < i; k++)
      for (int id : {kExtrasFirst + k, kExtrasLabelFirst + k})
        if (UiElement *e = this->getElementById(id))
          e->setHidden(k / kExtrasRows != this->extrasShownPage);
    if (UiText *text = dynamic_cast<UiText *>(this->getElementById(kExtrasPageText)))
      text->setText(std::to_string(this->extrasShownPage + 1) + " / " + std::to_string(pages));
    if (UiElement *e = this->getElementById(kExtrasPrev))
      e->setDisabled(this->extrasShownPage == 0);
    if (UiElement *e = this->getElementById(kExtrasNext))
      e->setDisabled(this->extrasShownPage >= pages - 1);
  };
  turnTo(0);
  if (UiButton *b = dynamic_cast<UiButton *>(this->getElementById(kExtrasPrev)))
    b->onClick = [this, turnTo] { turnTo(this->extrasShownPage - 1); };
  if (UiButton *b = dynamic_cast<UiButton *>(this->getElementById(kExtrasNext)))
    b->onClick = [this, turnTo] { turnTo(this->extrasShownPage + 1); };
}

void UiGameScreen::showTab(int panelId, int buttonId) {
  for (Entry &e : this->entries) {
    if (!e.panel || e.id != panelId)
      continue;
    if (UiButton *b = dynamic_cast<UiButton *>(e.layout->getElementById(buttonId)))
      b->choose();
    e.layout->syncTabs();
  }
  for (BuyPanel &p : this->buyPanels)
    this->refreshBuyPanel(p);
  this->refreshStaffPlaque();
  this->refreshAdoptPlaque();
  this->refreshTerraform();
  this->refreshResearch();
  this->refreshZooStatus();
}

void UiGameScreen::setPanelOpen(int id, bool open) {
  // Opening Adopt (3), Objects (4), Build Exhibit (8), Hire Staff (10),
  // Research (15) or Game Options (5) puts the bulldozer away (zoo.exe
  // buya_Panel_show 0x4e3917, buyobj_Panel_show 0x4e49f5, buldhab_Panel_show
  // 0x4da843, personel_Panel_show 0x4e6fc2, research_Panel_show 0x4a031a:
  // bulldozeToolOff 0x443b60)
  if (open && !this->isPanelOpen(id) && (id == 3 || id == 4 || id == 8 || id == 10 || id == 15 || id == 5))
    this->bulldozerOff();
  // Build Exhibit shut: gate mode off (zoo.exe buldhab_Panel_hide 0x4e10d3:
  // 0x4e1064(0))
  if (id == 8 && !open && this->isPanelOpen(8))
    this->setGateMode(false);
  // The Game Options (5): the Options window in its place (the optionswindow
  // extra), the disk button left off
  if (id == 5 && open && Features::optionsWindow && this->options) {
    // (the disk button again closes it, as it closes the original's panel)
    if (this->options->isOpen())
      this->options->close();
    else
      this->options->open(true);
    for (UiButton *b : this->panelButtons)
      if (b->getActionTarget() == 5)
        b->setToggledOn(false);
    open = false;
  }
  // (ours, infocloseslists: a bottom list opened over an information window
  // closes the window, and a carer's Recommendations with it - they never
  // overlap)
  if (open && Features::infoClosesLists && (id == 30 || id == 31 || id == 16 || id == 12))
    for (Entry &e : this->entries)
      if (e.panel && e.open && (e.id == 6 || e.id == 9 || e.id == 17 || e.id == 20 || e.id == 21 || e.id == 28 || e.id == 1400))
        e.open = false;
  // A list at the bottom right opened: Zoo Status and the show panel shut
  // (closeOtherPanels 0x443af8 with 0: 0xe, 0x323, 0x29), and from the
  // Staff and Guest Lists the animal plaque and a carer's Recommendations
  // too (0x44410c(0): 0x1b, 0x1c) - mulstaff_Panel_show 0x469fc4,
  // mulguest_Panel_show 0x469cc8, mulanim_Panel_show 0x469627 (no
  // 0x44410c); the information windows move above it (0x4ef99b, in
  // panelRect). Their Panel_hide - mulstaff 0x469ded, mulanim 0x46954b -
  // let the HUD button go (slot 0x54) and move the windows back. (ours,
  // smartstack: the other windows stay)
  if (open && !this->isPanelOpen(id) && (id == 16 || id == 12 || id == 31) && !Features::smartStack) {
    std::vector<int> shut = {14, 41};
    if (id != 16) {
      shut.push_back(27);
      shut.push_back(28);
    }
    for (int p : shut) {
      this->setPanelOpen(p, false);
      for (UiButton *b : this->panelButtons)
        if (b->getActionTarget() == p)
          b->setToggledOn(false);
    }
  }
  // Hire Staff opened with its pick let go: the last staff type picked
  // again, else the first (zoo.exe personel_Panel_show 0x4e6fc2: the type
  // 0x63e474 looked up in the refilled list, else 0, selected - 0x4e9fea -
  // and personel_Staff_click 0x4e69b8 run on it)
  if (id == 10 && open && !this->isPanelOpen(10))
    for (BuyPanel &p : this->buyPanels)
      if (p.id == 10 && p.region && p.region->getSelected() < 0) {
        p.category.clear();
        this->refreshBuyPanel(p);
      }
  // The scenario page opening fills itself (zooinfo4_Panel_show 0x4efdf6:
  // 0x4f004a)
  if (id == 803 && open && !this->isPanelOpen(803))
    this->refreshScenarioPage();
  for (Entry &e : this->entries)
    if (e.panel && e.id == id) {
      if (open && !e.open)
        e.openedAt = ++this->openCount;
      // (the Exhibit List opened or shut: no exhibit picked - 0x58b5ce(0)
      // in mulhab_Panel_show 0x46836c and mulhab_Panel_hide 0x4682f1; shut,
      // a selected gate deselected too - 0x4ef75b(0, 1), measured: the
      // arrow over it gone, #266)
      if (id == 30 && open != e.open && this->exhibitPanel) {
        if (UiListBox *list = dynamic_cast<UiListBox *>(this->exhibitPanel->getElementById(4305)))
          list->setSelectedIndex(-1);
        // (and not picked again when the list is relisted: DAT_0063915c 0)
        this->shownExhibit = -1;
      }
      if (id == 30 && open != e.open)
        this->gateExhibit = -1;
      if (id == 30 && open && !e.open)
        this->exhibitPagesDue = true;
      e.open = open;
    }
}

// An information window opens: the toolbar's panels close but an open
// list at the bottom right stays (the window goes above it), and only one
// information window shows at a time
// (ours, the infocloseslists switch: the list closes too, so it can't
// cover the window)
void UiGameScreen::openInfo(int id) {
  // (ours, smartstack: nothing else closes - the windows stand side by
  // side - only another information window, which this one replaces)
  if (Features::smartStack && !Features::infoClosesLists) {
    for (int p : {6, 9, 17, 20, 21, 1400})
      this->setPanelOpen(p, p == id);
    return;
  }
  std::vector<int> lists;
  for (int l : {30, 31, 16, 12})
    if (this->isPanelOpen(l) && !Features::infoClosesLists)
      lists.push_back(l);
  // (closed so, each list is built afresh when it next opens: its last pick
  // kept, it showed that one again - which closed the list again)
  if (Features::infoClosesLists) {
    this->staffListCount = -1;
    this->listedStaff.clear();
    this->listedAnimals.clear();
    this->listedGuests.clear();
    this->listedExhibits.clear();
  }
  this->showPanel(0, "");
  for (int l : lists) {
    this->setPanelOpen(l, true);
    for (UiButton *b : this->panelButtons)
      if (b->getActionTarget() == l)
        b->setToggledOn(true);
  }
  // (the DRT base's 1400 among them: one thing is selected at a time, and
  // a new selection shuts the last one's panel - zoo.exe 0x5b66b1,
  // ui_drt_50f5fc)
  for (int p : {6, 9, 17, 20, 21, 1400})
    this->setPanelOpen(p, p == id);
  // (and the last animal's Recommendations: ours, with the switch)
  if (Features::infoClosesLists)
    this->setPanelOpen(28, false);
}

bool UiGameScreen::closeTopmost() {
  // (the Options window: Esc closes it, and it takes the Esc itself)
  if (this->optionsOpen())
    return true;
  if (this->filter.open) {
    this->filter.open = false;
    return true;
  }
  if (this->dialog.layout) {
    // A question is answered No; a name to type has to be given
    if (!this->dialog.textId)
      this->closeDialog();
    return true;
  }
  Entry *top = nullptr;
  for (Entry &e : this->entries)
    if (e.panel && e.open && (!top || e.openedAt > top->openedAt))
      top = &e;
  if (!top)
    return false;
  int id = top->id;
  this->setPanelOpen(id, false);
  for (UiButton *b : this->panelButtons)
    if (b->getActionTarget() == id)
      b->setToggledOn(false);
  return true;
}

// A panel sits where its [LayoutInfo] says, inside the HUD element it is
// anchored to (the buy panels: anchor=1032, the toolbar column) or the
// whole screen
SDL_Rect UiGameScreen::anchorRect(const Entry &e, SDL_Rect *screen) {
  if (e.layout->getAnchor() != 0 && this->hud) {
    if (UiElement *a = this->hud->getElementById(e.layout->getAnchor())) {
      SDL_Rect r = a->getLastRect();
      if (r.w > 0 || r.h > 0)
        return r;
    }
  }
  return *screen;
}

SDL_Rect UiGameScreen::panelRect(SDL_Renderer *renderer, const Entry &e,
                                 SDL_Rect *screen) {
  SDL_Rect parent = this->anchorRect(e, screen);
  // The paint tab and colour picker sit on the open Building Information
  // (anchor=21)
  if (e.id == 302 || e.id == 37)
    for (const Entry &o : this->entries)
      if (o.panel && o.open && (o.id == 21 || o.id == 20)) {
        // (where it is this frame: dragged or moved aside, it goes with it)
        SDL_Rect pr = o.rect.w > 0 ? o.rect : this->panelRect(renderer, o, screen);
        return e.layout->computeRect(renderer, &pr);
      }
  SDL_Rect r = e.layout->computeRect(renderer, &parent);
  // An information window (animal, guest, staff, building, tank filter,
  // DRT) with a list open at the bottom right (the Animal, Guest, Exhibit/
  // Show and Staff Lists: x=right, y=-366): at its layout's altx / alty
  // (right, -578), above the list (zoo.exe 0x4ef99b, from each list's
  // Panel_show - mulguest_Panel_show 0x469cc8 - and Panel_hide 0x469d97:
  // panels 6, 9, 17, 20, 21, 1400 and 46 to their second place and back,
  // 0x4ef967)
  if (IniReader *ei = e.layout->getIniReader(); ei && !ei->get("layoutinfo", "alty").empty())
    for (const Entry &o : this->entries) {
      if (!o.panel || !o.open || &o == &e)
        continue;
      IniReader *oi = o.layout->getIniReader();
      if (!oi || oi->get("layoutinfo", "x") != "right" || oi->get("layoutinfo", "y") != "-366")
        continue;
      const std::string ax = ei->get("layoutinfo", "altx"), ay = ei->get("layoutinfo", "alty");
      if (ax == "right")
        r.x = parent.x + parent.w - r.w;
      else if (!ax.empty())
        r.x = ax[0] == '-' ? parent.x + parent.w + std::atoi(ax.c_str()) : parent.x + std::atoi(ax.c_str());
      r.y = ay[0] == '-' ? parent.y + parent.h + std::atoi(ay.c_str()) : parent.y + std::atoi(ay.c_str());
      return r;
    }
  // A negative x or y counts from the right or bottom edge (the Exhibit
  // List's y=-366 puts it 366 above the bottom of the screen, at 234)
  IniReader *ini = e.layout->getIniReader();
  if (ini) {
    std::string x = ini->get("layoutinfo", "x"), y = ini->get("layoutinfo", "y");
    if (!x.empty() && x[0] == '-')
      r.x = parent.x + parent.w + std::atoi(x.c_str());
    if (!y.empty() && y[0] == '-')
      r.y = parent.y + parent.h + std::atoi(y.c_str());
  }
  return r;
}

// A press on a window's handle: the original's alias=-1 element (Zoo
// Status, Recommendations, a plaque...), or - the dragwindows extra - any
// window's title strip (its top 20 pixels); never on a control in it
bool UiGameScreen::lockable(const Entry &e) const {
  return Features::dragWindows && e.panel && e.closeId && e.id != 302 && e.id != 37;
}

bool UiGameScreen::beginDrag(int x, int y) {
  SDL_Point p = {x, y};
  Entry *top = nullptr;
  for (Entry &e : this->entries)
    if (e.panel && e.open && e.rect.w > 0 && SDL_PointInRect(&p, &e.rect) && (!top || e.openedAt > top->openedAt))
      top = &e;
  if (!top || top->id == 302 || top->id == 37)
    return false;
  // (its lock: held down here, opened or shut when let go on it)
  if (this->lockable(*top) && top->lockRect.w > 0 && SDL_PointInRect(&p, &top->lockRect)) {
    this->lockDownId = top->id;
    return true;
  }
  if (this->lockable(*top) && top->locked)
    return false;
  std::function<bool(UiElement *)> onControl = [&](UiElement *el) {
    if (el->isHidden())
      return false;
    if (!dynamic_cast<UiLayout *>(el) && !dynamic_cast<UiImage *>(el) && !dynamic_cast<UiStatusImage *>(el) &&
        !(dynamic_cast<UiText *>(el) && !static_cast<UiText *>(el)->isEditable())) {
      SDL_Rect r = el->getLastRect();
      if (r.w > 0 && SDL_PointInRect(&p, &r))
        return true;
    }
    for (UiElement *c : el->getChildren())
      if (onControl(c))
        return true;
    return false;
  };
  if (onControl(top->layout))
    return false;
  bool on = false;
  if (top->handle)
    if (UiElement *h = top->layout->getElementById(top->handle)) {
      SDL_Rect hr = h->getLastRect();
      on = SDL_PointInRect(&p, &hr);
    }
  if (!on && Features::dragWindows)
    on = y < top->rect.y + 20;
  if (!on)
    return false;
  this->dragId = top->id;
  this->dragGrabX = x - top->rect.x;
  this->dragGrabY = y - top->rect.y;
  this->dragFromX = x;
  this->dragFromY = y;
  this->dragMoving = false;
  top->openedAt = ++this->openCount;
  return true;
}

// Where each open window goes this frame, in the order they opened: where
// the player dragged it; else where the original puts it - and, with the
// smartstack extra, beside an earlier window it would cover (above it, to
// its left, below or right: the nearest spot on the screen clear of them)
void UiGameScreen::placePanels(SDL_Renderer *renderer, SDL_Rect *screen) {
  std::vector<Entry *> order;
  for (Entry &e : this->entries)
    if (e.panel && e.open)
      order.push_back(&e);
  std::stable_sort(order.begin(), order.end(), [](const Entry *a, const Entry *b) { return a->openedAt < b->openedAt; });
  // (the paint tab and colour picker sit on Building Information: part of it)
  auto partOf = [](int id) { return id == 302 || id == 37; };
  std::vector<SDL_Rect> placed, bars;
  // (a window moved aside keeps off the toolbar and the bottom bar too:
  // main.lyt's root images)
  for (int id = 1032; id <= 1039; id++)
    if (UiElement *b = this->hud ? this->hud->getElementById(id) : nullptr) {
      SDL_Rect br = b->getLastRect();
      if (br.w > 0 && br.h > 0)
        bars.push_back(br);
    }
  for (Entry *e : order) {
    SDL_Rect r = this->panelRect(renderer, *e, screen);
    if (e->moved) {
      // (its title kept on the screen)
      r.x = std::clamp(e->moveX, screen->x - r.w + 60, screen->x + screen->w - 60);
      r.y = std::clamp(e->moveY, screen->y, screen->y + screen->h - 24);
    } else if (Features::smartStack && !partOf(e->id) && r.w > 0 && r.h > 0) {
      auto clear = [&](const SDL_Rect &c, bool ofBars) {
        if (c.x < screen->x || c.y < screen->y || c.x + c.w > screen->x + screen->w || c.y + c.h > screen->y + screen->h)
          return false;
        for (const SDL_Rect &q : placed)
          if (SDL_HasRectIntersection(&c, &q))
            return false;
        if (ofBars)
          for (const SDL_Rect &q : bars)
            if (SDL_HasRectIntersection(&c, &q))
              return false;
        return true;
      };
      if (!clear(r, false)) {
        SDL_Rect best = r;
        long bestD = -1;
        for (const SDL_Rect &q : placed)
          for (SDL_Rect c : {SDL_Rect{r.x, q.y - r.h, r.w, r.h}, SDL_Rect{q.x - r.w, r.y, r.w, r.h},
                             SDL_Rect{r.x, q.y + q.h, r.w, r.h}, SDL_Rect{q.x + q.w, r.y, r.w, r.h},
                             SDL_Rect{q.x - r.w, q.y + q.h - r.h, r.w, r.h}, SDL_Rect{q.x + q.w - r.w, q.y - r.h, r.w, r.h}}) {
            if (!clear(c, true))
              continue;
            long d = static_cast<long>(c.x - r.x) * (c.x - r.x) + static_cast<long>(c.y - r.y) * (c.y - r.y);
            if (bestD < 0 || d < bestD) {
              bestD = d;
              best = c;
            }
          }
        r = best;
      }
    }
    e->rect = r;
    if (!partOf(e->id))
      placed.push_back(r);
  }
}

// addition: approved session-only 1/2/5 speed selector. The game owns the clock.
void UiGameScreen::setGameSpeed(int speed) {
  this->selectedSpeed = Features::fastForward && (speed == 2 || speed == 5) ? speed : 1;
  if (!Features::fastForward) this->speedDown = false;
}

SDL_Rect UiGameScreen::speedButtonRect() const {
  return Features::fastForward ? this->speedRect : SDL_Rect{};
}

// addition: measured placement of the approved 1448x1086 artwork in the
// original 800x600 HUD coordinates.
static SDL_FRect speedArtworkRect(const SDL_Rect &pause) {
  constexpr float referenceScale = 800.0f / 1448.0f;
  return {pause.x + 304*referenceScale - 171,
          pause.y + 1020*referenceScale - 566,
          UiSpeedControlArt::frameWidth*referenceScale,
          UiSpeedControlArt::frameHeight*referenceScale};
}

// addition: fit the original red or blue face to the approved bronze lobe.
// The bronze-to-olive boundary supplies the coverage. At the shared waist
// the color feathers into the speed control's bronze face.
static SDL_Texture *makeSpeedTransportFace(SDL_Renderer *renderer, SDL_Surface *nativeFace) {
  SDL_IOStream *bytes = SDL_IOFromConstMem(UiSpeedControlArt::png, sizeof(UiSpeedControlArt::png));
  SDL_Surface *atlas = bytes ? IMG_Load_IO(bytes, 1) : nullptr;
  if (!atlas) return nullptr;
  SDL_Surface *face = SDL_ConvertSurface(atlas, SDL_PIXELFORMAT_RGBA32);
  SDL_DestroySurface(atlas);
  if (!face) return nullptr;

  constexpr int width = UiSpeedControlArt::frameWidth;
  constexpr int height = UiSpeedControlArt::frameHeight;
  // Measured in the selected artwork: left face center and the neck before
  // the speed arrows. Ease the color across that neck to avoid a visible seam.
  constexpr float centerX = 35.5f, centerY = 34.5f, waistStart = 48, waistEnd = 65;
  // Sample inside the original colored face, before its separate olive rim.
  constexpr float nativeRadius = 11.0f;
  SDL_Rect frame{width, 0, width, height};
  for (int y = 0; y < height; ++y) {
    auto *row = reinterpret_cast<Uint32 *>(static_cast<Uint8 *>(face->pixels) + y * face->pitch);
    for (int x = 0; x < width; ++x) {
      Uint32 &pixel = row[frame.x + x];
      Uint8 r, g, b, a;
      ZT_GetRGBA(pixel, face->format, &r, &g, &b, &a);
      // The lit lower edge is still bronze where red and green nearly match.
      // Its stronger red-to-blue contrast distinguishes it from the olive rim.
      const float warmth = std::max((int(r) - int(g)) / 14.0f, (int(r) - int(b) - 28) / 36.0f);
      const float bronze = std::clamp(warmth, 0.0f, 1.0f);
      const float blend = std::clamp((waistEnd - (x + 0.5f)) / (waistEnd - waistStart), 0.0f, 1.0f);
      const float waist = blend * blend * (3.0f - 2.0f * blend);
      const Uint8 alpha = static_cast<Uint8>(a * bronze * waist);
      if (!alpha) {
        pixel = 0;
        continue;
      }

      float dx = (x + 0.5f - centerX) / 2.0f;
      float dy = (y + 0.5f - centerY) / 2.0f;
      const float radius = std::hypot(dx, dy);
      if (radius > nativeRadius) {
        dx *= nativeRadius / radius;
        dy *= nativeRadius / radius;
      }
      const int sx = std::clamp(int((16.5f + dx) * nativeFace->w / 32), 0, nativeFace->w - 1);
      const int sy = std::clamp(int((15.5f + dy) * nativeFace->h / 32), 0, nativeFace->h - 1);
      const auto *sample = static_cast<const Uint8 *>(nativeFace->pixels) + sy * nativeFace->pitch + sx * SDL_BYTESPERPIXEL(nativeFace->format);
      Uint32 color = 0;
      std::memcpy(&color, sample, SDL_BYTESPERPIXEL(nativeFace->format));
      ZT_GetRGB(color, nativeFace->format, &r, &g, &b);
      pixel = ZT_MapRGBA(face->format, r, g, b, alpha);
    }
  }

  SDL_Surface *overlay = SDL_CreateSurface(width, height, SDL_PIXELFORMAT_RGBA32);
  SDL_Texture *texture = nullptr;
  if (overlay) {
    SDL_SetSurfaceBlendMode(face, SDL_BLENDMODE_NONE);
    if (SDL_BlitSurface(face, &frame, overlay, nullptr))
      texture = SDL_CreateTextureFromSurface(renderer, overlay);
    SDL_DestroySurface(overlay);
  }
  SDL_DestroySurface(face);
  return texture;
}

// addition: import the selected component itself, retaining its face, rim and symbols.
void UiGameScreen::drawSpeedControl(SDL_Renderer *renderer, const SDL_Rect &pause) {
  if (pause.w <= 0 || pause.h <= 0) return;
  const SDL_FRect joined = speedArtworkRect(pause);
  this->speedRect = {pause.x + pause.w - 2, pause.y + 4, 22, pause.h - 8};
  if (!RenderSettings::drawsUiArt()) return;
  if (!this->speedArtwork) {
    SDL_IOStream *bytes = SDL_IOFromConstMem(UiSpeedControlArt::png, sizeof(UiSpeedControlArt::png));
    if (bytes) this->speedArtwork = IMG_LoadTexture_IO(renderer, bytes, 1);
  }
  SDL_Point pointer{this->mouseX,this->mouseY};
  const bool hover = SDL_PointInRect(&pointer,&this->speedRect);
  const int frame = this->selectedSpeed == 5 ? 2 : this->selectedSpeed == 2 ? 1 : 0;
  const SDL_Rect source{frame*UiSpeedControlArt::frameWidth,0,
                        UiSpeedControlArt::frameWidth,UiSpeedControlArt::frameHeight};
  if (this->speedArtwork) {
    SDL_SetTextureBlendMode(this->speedArtwork,SDL_BLENDMODE_BLEND);
    SDL_SetTextureScaleMode(this->speedArtwork,SDL_SCALEMODE_LINEAR);
    SDL_SetTextureColorMod(this->speedArtwork,255,255,255);
    ZT_RenderCopyF(renderer,this->speedArtwork,&source,&joined);
    // (as a lit button: brighter under the pointer, and pressed in - darker,
    // a pixel lower - while held and for a moment after a quick click)
    const bool pressed = this->speedDown || SDL_GetTicks() < this->speedPressUntil;
    if (hover || pressed) {
      SDL_Rect previous{};
      const bool clipped = SDL_RenderClipEnabled(renderer) == true;
      SDL_GetRenderClipRect(renderer,&previous);
      SDL_Rect clip = this->speedRect;
      if (clipped) SDL_GetRectIntersection(&previous,&this->speedRect,&clip);
      SDL_SetRenderClipRect(renderer,&clip);
      if (pressed) {
        // (the lobe drawn again over itself, darkened and nudged down)
        SDL_FRect step = joined;
        step.y += 1.0f;
        SDL_SetTextureColorMod(this->speedArtwork,150,140,120);
        ZT_RenderCopyF(renderer,this->speedArtwork,&source,&step);
      } else {
        SDL_SetTextureBlendMode(this->speedArtwork,SDL_BLENDMODE_ADD);
        SDL_SetTextureColorMod(this->speedArtwork,255,235,160);
        SDL_SetTextureAlphaMod(this->speedArtwork,90);
        ZT_RenderCopyF(renderer,this->speedArtwork,&source,&joined);
        SDL_SetTextureAlphaMod(this->speedArtwork,255);
        SDL_SetTextureBlendMode(this->speedArtwork,SDL_BLENDMODE_BLEND);
      }
      SDL_SetRenderClipRect(renderer,clipped ? &previous : nullptr);
      SDL_SetTextureColorMod(this->speedArtwork,255,255,255);
    }
  }
  // All rates reuse the same selected face and sampled fixed-size symbol.
}

void UiGameScreen::setDateTime(const std::string &calendar, const std::string &time) {
  if (!this->hud) return;
  this->dateTime = time;
  if (time.empty()) this->dateTimeWell = {};
  if (auto *date = dynamic_cast<UiText *>(this->hud->getElementById(1030))) {
    date->setText(calendar);
    date->setHidden((!time.empty() || Features::fastForward) && this->dateTimeText != nullptr);
  }
  const std::string label = time.empty() ? calendar : calendar + "   " + time;
  if (this->dateTimeText && this->dateTimeText->getText() != label) this->dateTimeText->setMessage(label);
}

// addition: extend the native date recess horizontally for the visual clock.
// ui/main.lyt supplies the text role and positions; backgnd3/backgnd4 supply
// the recessed edges. Only the middle stretches, leaving the round control alone.
void UiGameScreen::drawDateTime(SDL_Renderer *renderer) {
  this->dateTimeWell = {};
  this->speedRect = {};
  if ((this->dateTime.empty() && !Features::fastForward) || !this->dateTimeText) return;
  auto *money = dynamic_cast<UiText *>(this->hud->getElementById(1016));
  auto *moneyBackground = this->hud->getElementById(1037);
  auto drawOriginalMoney = [&] {
    if (money && moneyBackground && !money->isHidden()) {
      SDL_Rect parent = moneyBackground->getLastRect();
      money->draw(renderer,&parent);
    }
  };
  auto *pause = this->hud->getElementById(1071);
  auto *play = this->hud->getElementById(1072);
  UiElement *shown = pause && !pause->isHidden() ? pause : play;
  auto *background = this->hud->getElementById(1035);
  auto *nativeDate = this->hud->getElementById(1030);
  if (!shown || shown->isHidden() || !background || !nativeDate || !pause) {
    drawOriginalMoney();
    return;
  }
  auto *ini = this->hud->getIniReader();
  const SDL_Rect back = background->getLastRect();
  Animation *art = this->resource_manager->sharedAnimation("ui/main/backgnd3/backgnd3");
  int width = 0, height = 0;
  if (art) art->queryTexture(CompassDirection::N, &width, &height);
  const int left = ini->getInt(pause->getName(), "x") + ini->getInt(nativeDate->getName(), "x");
  const int originalWidth = ini->getInt(nativeDate->getName(), "dx");
  const int textTop = ini->getInt(pause->getName(), "y") + ini->getInt(nativeDate->getName(), "y");
  // measured: native backgnd3's upper recessed edge starts at the Date box
  // top (before its 2 px text border), not 3 px below it. Its bottom is
  // 5 px above the background's bottom. Keep the complete
  // 12 px corner/shoulder at each end: the curved upper lip extends beyond
  // the innermost 4 px and must not be stretched into the straight middle.
  constexpr int endcap = 12, moneyTextInset = 4, trailingPadding = 4, wellGap = 6;
  const SDL_Rect source{left, textTop, originalWidth, height - 5 - textTop};
  if (left < 0 || source.x + source.w > width || source.h <= 0) {
    drawOriginalMoney();
    return;
  }
  int labelWidth = 0, labelHeight = 0;
  if (SDL_Texture *text = this->resource_manager->getStringTexture(renderer,
          ini->getInt(nativeDate->getName(), "font"), this->dateTimeText->getText(),
          SDL_Color{255, 255, 255, 255}, ini->getInt(nativeDate->getName(), "fontsize")))
    this->resource_manager->getTextSize(text, &labelWidth, &labelHeight);
  int calendarWidth = 0, calendarHeight = 0;
  if (auto *calendar = dynamic_cast<UiText *>(nativeDate))
    if (SDL_Texture *text = this->resource_manager->getStringTexture(renderer,
            ini->getInt(nativeDate->getName(), "font"), calendar->getText(),
            SDL_Color{255, 255, 255, 255}, ini->getInt(nativeDate->getName(), "fontsize")))
      this->resource_manager->getTextSize(text, &calendarWidth, &calendarHeight);
  const int border = ini->getInt(nativeDate->getName(), "border", 0);
  const int leading = std::max(0, (originalWidth - 2 * border - calendarWidth) / 2);
  SDL_Rect well{back.x + left, back.y + source.y,
                std::max(originalWidth, labelWidth + leading + 2 * border + trailingPadding), source.h};
  // addition: the approved smaller attached speed face adds 20 native pixels.
  // The well starts underneath its rim; only its straight middle stretches.
  constexpr int speedWidth = 20;
  if (Features::fastForward) well.x += speedWidth;
  this->dateTimeWell = well;

  auto region = [&](Animation *image, int imageWidth, int imageHeight, const SDL_Rect &src, const SDL_Rect &dst) {
    if (!image || imageWidth <= 0 || imageHeight <= 0 || !RenderSettings::drawsUiArt()) return;
    const SDL_FPoint dest[]{{float(dst.x),float(dst.y)},{float(dst.x+dst.w),float(dst.y)},
                           {float(dst.x+dst.w),float(dst.y+dst.h)},{float(dst.x),float(dst.y+dst.h)}};
    const SDL_FPoint uv[]{{float(src.x)/imageWidth,float(src.y)/imageHeight},
                         {float(src.x+src.w)/imageWidth,float(src.y)/imageHeight},
                         {float(src.x+src.w)/imageWidth,float(src.y+src.h)/imageHeight},
                         {float(src.x)/imageWidth,float(src.y+src.h)/imageHeight}};
    image->drawMapped(renderer,CompassDirection::N,0,dest,uv,{255,255,255,255});
  };
  auto recess = [&](Animation *image, int imageWidth, int imageHeight, const SDL_Rect &src, const SDL_Rect &dst) {
    const int sourceX[]{src.x,src.x+endcap,src.x+src.w-endcap,src.x+src.w};
    const int targetX[]{dst.x,dst.x+endcap,dst.x+dst.w-endcap,dst.x+dst.w};
    for (int x = 0; x < 3; ++x)
      region(image,imageWidth,imageHeight,{sourceX[x],src.y,sourceX[x+1]-sourceX[x],src.h},
             {targetX[x],dst.y,targetX[x+1]-targetX[x],dst.h});
  };

  SDL_Rect moneyWell{};
  SDL_Rect moneyBack{};
  int statusShift = 0;
  if (money && moneyBackground) {
    moneyBack = moneyBackground->getLastRect();
    // measured: backgnd4's money recess encloses Money with 17 px on the
    // left and 6 on the right; retain its native right edge and baseline.
    const SDL_Rect moneySource{ini->getInt(money->getName(), "x") - 17,
                              ini->getInt(money->getName(), "y") - 1,
                              ini->getInt(money->getName(), "dx") + 23, source.h};
    const SDL_Rect originalMoney{moneyBack.x + moneySource.x,moneyBack.y + moneySource.y,moneySource.w,moneySource.h};
    const int speedShift = Features::fastForward ? speedWidth : 0;
    const int overlap = std::max(0, well.x + well.w + wellGap - originalMoney.x - speedShift);
    if (overlap > 0 && overlap < originalMoney.w - 2 * endcap) {
      Animation *moneyArt = this->resource_manager->sharedAnimation("ui/main/backgnd4/backgnd4");
      Animation *filler = this->resource_manager->sharedAnimation("ui/main/bg3/bg3");
      int mw = 0, mh = 0, fw = 0, fh = 0;
      if (moneyArt) moneyArt->queryTexture(CompassDirection::N,&mw,&mh);
      if (filler) filler->queryTexture(CompassDirection::N,&fw,&fh);
      if (mw > 0 && mh > 0 && fw > 0 && fh >= mh - moneySource.y) {
        // Repaint the old well and text with the native plain bar, aligned
        // at its bottom as Filler3 is, then put back only the narrower well.
        region(filler,fw,fh,{0,fh-mh+moneySource.y,fw,moneySource.h},originalMoney);
        if (speedShift > 0) {
          statusShift = speedShift;
          // addition: borrow the extra width from the Zoo Status meter,
          // retaining Money's clock-only width and every control to its right.
          if (auto *meter = this->hud->getElementById(1080)) {
            const SDL_Rect oldMeter = meter->getLastRect();
            const SDL_Rect clear{originalMoney.x, moneyBack.y,
                                 oldMeter.x+oldMeter.w-originalMoney.x, mh};
            region(filler,fw,fh,{0,fh-mh,fw,mh},clear);
          }
        }
        moneyWell = {originalMoney.x+overlap+speedShift,originalMoney.y,originalMoney.w-overlap,originalMoney.h};
        recess(moneyArt,mw,mh,moneySource,moneyWell);
      }
    }
  }
  if (Features::fastForward) {
    // addition: cover the vacated date lip with the player's native plain bar.
    Animation *filler = this->resource_manager->sharedAnimation("ui/main/bg3/bg3");
    int fw = 0, fh = 0;
    if (filler) filler->queryTexture(CompassDirection::N, &fw, &fh);
    if (fw > 0 && fh >= source.h + 5)
      region(filler,fw,fh,{0,fh-source.h-5,fw,source.h},
             {back.x+left,back.y+source.y,speedWidth,source.h});
  }
  recess(art,width,height,source,well);
  // The round button stays in front, with exactly its original hit rectangle.
  SDL_Rect buttonParent = back;
  if (Features::fastForward) {
    // addition: retain the native element's rectangle and input, but the
    // approved shared component replaces its normal face and rim. The native
    // element continues to own its original hit rectangle and Pause action.
    const auto pass = RenderSettings::uiPass;
    RenderSettings::uiPass = RenderSettings::UiPass::Text;
    shown->draw(renderer,&buttonParent);
    RenderSettings::uiPass = pass;
    this->drawSpeedControl(renderer, shown->getLastRect());
    if (RenderSettings::drawsUiArt()) {
      SDL_Rect face = shown->getLastRect();
      auto *nativeButton = dynamic_cast<UiButton *>(shown);
      Animation *native = nativeButton ? this->resource_manager->sharedAnimation(nativeButton->getAnimationPath()) : nullptr;
      SDL_Point pointer{this->mouseX,this->mouseY};
      const bool hover = SDL_PointInRect(&pointer,&face) && !SDL_PointInRect(&pointer,&this->speedRect);
      const int key = (shown == play ? 2 : 0) + (hover ? 1 : 0);
      // Each state is fitted to the shared artwork once and cached.
      if (native && (shown == play || hover) && !this->speedNativeFaces[key]) {
        SDL_Surface *sourceFace = native->frameSurface(hover ? "H" : "N",0);
        if (!sourceFace) sourceFace = native->frameSurface("N",0);
        if (sourceFace)
          this->speedNativeFaces[key] = makeSpeedTransportFace(renderer, sourceFace);
      }
      if (SDL_Texture *texture = this->speedNativeFaces[key]; texture && (shown == play || hover)) {
        SDL_SetTextureBlendMode(texture,SDL_BLENDMODE_BLEND);
        SDL_SetTextureScaleMode(texture,SDL_SCALEMODE_LINEAR);
        const SDL_FRect joined = speedArtworkRect(face);
        ZT_RenderCopyF(renderer,texture,nullptr,&joined);
      }
    }
  } else {
    shown->draw(renderer,&buttonParent);
  }
  this->dateTimeIni->set("date", "justify", "left");
  this->dateTimeIni->set("date", "x", std::to_string(leading));
  this->dateTimeIni->set("date", "dx", std::to_string(well.w - leading));
  SDL_Rect textBox{well.x,back.y + textTop,well.w,well.h};
  this->dateTimeText->draw(renderer,&textBox);
  if (moneyWell.w > 0 && !money->isHidden()) {
    const std::string section = money->getName();
    const std::string x = ini->get(section,"x"), dx = ini->get(section,"dx"), justify = ini->get(section,"justify");
    ini->set(section,"x",std::to_string(moneyWell.x-moneyBack.x+moneyTextInset));
    ini->set(section,"dx",std::to_string(moneyWell.w-2*moneyTextInset));
    ini->set(section,"justify","center");
    money->draw(renderer,&moneyBack);
    ini->set(section,"x",x);
    ini->set(section,"dx",dx);
    ini->set(section,"justify",justify);
  } else {
    drawOriginalMoney();
  }
  if (statusShift > 0)
    if (auto *statusButton = this->hud->getElementById(1014)) {
      SDL_Rect parent = moneyBack;
      parent.x += statusShift;
      statusButton->draw(renderer,&parent);
      SDL_Rect button = statusButton->getLastRect();
      auto *meter = this->hud->getElementById(1080);
      auto *value = this->hud->getElementById(1015);
      if (meter && value) {
        const std::string valueWidth = ini->get(value->getName(),"dx");
        Animation *meterArt = this->resource_manager->sharedAnimation("ui/main/progbck/progbck");
        int mw = 0, mh = 0;
        if (meterArt) meterArt->queryTexture(CompassDirection::N,&mw,&mh);
        ini->set(value->getName(),"dx",std::to_string(std::max(1,std::atoi(valueWidth.c_str())-statusShift)));
        // Draw from the native texture without inserting a missing dx key
        // into its layout: restoring that key as empty changes native sizing.
        const SDL_Rect meterBox{button.x+ini->getInt(meter->getName(),"x"),
                                 button.y+ini->getInt(meter->getName(),"y"),
                                 std::max(1,mw-statusShift),mh};
        region(meterArt,mw,mh,{0,0,mw,mh},meterBox);
        value->draw(renderer,&button);
        // The circular button overlaps the left end of its meter natively.
        statusButton->draw(renderer,&parent);
        ini->set(value->getName(),"dx",valueWidth);
      }
    }
}

void UiGameScreen::draw(SDL_Renderer *renderer, SDL_Rect *layout_rect) {
  this->wrapHelp(renderer);
  // The gate button, live: enabled the moment there's an exhibit (the first
  // one finished with the panel open left it greyed until it was opened
  // again)
  if (this->isPanelOpen(8))
    for (BuyPanel &p : this->buyPanels)
      if (p.id == 8 && p.gate)
        if (UiElement *gate = p.layout->getElementById(p.gate)) {
          gate->setHidden(p.category != "fence");
          gate->setDisabled(!this->exhibitsExist || !this->exhibitsExist());
        }
  this->dragUiScale(renderer);
  this->placePanels(renderer, layout_rect);
  for (Entry &e : this->entries) {
    if (!e.open)
      continue;
    if (!e.panel) {
      // addition: the optional clock owns Money's single draw, including in
      // a separate text pass where repainting its old position cannot erase it.
      UiElement *money = e.layout == this->hud && (!this->dateTime.empty() || Features::fastForward) && this->dateTimeText
                             ? this->hud->getElementById(1016) : nullptr;
      const bool moneyHidden = money && money->isHidden();
      if (money) money->setHidden(true);
      // addition: date visibility is recomputed from the switch each draw,
      // including after disabling the feature without a calendar tick.
      if (e.layout == this->hud)
        if (auto *date = this->hud->getElementById(1030))
          date->setHidden((!this->dateTime.empty() || Features::fastForward) && this->dateTimeText != nullptr);
      UiElement *pause = e.layout == this->hud && Features::fastForward ? this->hud->getElementById(1071) : nullptr;
      UiElement *play = e.layout == this->hud && Features::fastForward ? this->hud->getElementById(1072) : nullptr;
      const bool pauseHidden = pause && pause->isHidden(), playHidden = play && play->isHidden();
      if (pause) pause->setHidden(true);
      if (play) play->setHidden(true);
      e.layout->draw(renderer, layout_rect);
      if (pause) pause->setHidden(pauseHidden);
      if (play) play->setHidden(playHidden);
      if (money) money->setHidden(moneyHidden);
      if (e.layout == this->hud) this->drawDateTime(renderer);
      continue;
    }
    SDL_Rect r = e.rect;
    e.layout->setAnchorRect(this->anchorRect(e, layout_rect));
    e.layout->draw(renderer, &r);
    // (its lock, at the title bar's left end)
    e.lockRect = {0, 0, 0, 0};
    if (this->lockable(e))
      if (UiElement *c = e.layout->getElementById(e.closeId)) {
        SDL_Rect cr = c->getLastRect();
        if (cr.w > 0 && !c->isHidden()) {
          e.lockRect = WindowLock::place(e.layout, e.rect, cr);
          SDL_Point m = {this->mouseX, this->mouseY};
          bool over = SDL_PointInRect(&m, &e.lockRect);
          WindowLock::draw(renderer, this->resource_manager, e.lockRect, e.locked, over, over && this->lockDownId == e.id);
        }
      }
  }
  this->drawStaffPortrait(renderer);
  this->drawAnimalPortrait(renderer);
  this->drawMessage(renderer, layout_rect);
  // (the Options window goes over all this at a size of its own: main.cpp's
  // drawOptions)
  this->drawTooltip(renderer, layout_rect);
}

bool UiGameScreen::optionsOpen() const { return this->options && this->options->isOpen(); }

// The original Game Options panel's live settings, each frame before the
// panels draw: Interface Size (UiScaleControl), and Window / Full Screen
void UiGameScreen::dragUiScale(SDL_Renderer *renderer) {
  if (this->isPanelOpen(5))
    this->panelScale.update(renderer);
  // (Window / Full Screen: whichever it can change to, kept up to date -
  // Alt+Enter or F11 changes it too, with the Game Options open)
  if (this->isPanelOpen(5)) // (gameopts.lyt's id)
    if (SDL_Window *w = SDL_GetKeyboardFocus()) {
      bool full = (SDL_GetWindowFlags(w) & SDL_WINDOW_FULLSCREEN) != 0;
      if (UiElement *e = this->getElementById(1587))
        e->setHidden(!full);
      if (UiElement *e = this->getElementById(1588))
        e->setHidden(full);
    }
}

// The button under the cursor with help (the topmost shown one), else over
// the map the tool's hint
std::string UiGameScreen::tooltipText() const {
  if (this->dialog.layout || this->mouseX < 0)
    return "";
  SDL_Point p = {this->mouseX, this->mouseY};
  bool speedCovered = false;
  for (const Entry &e : this->entries)
    if (e.panel && e.open && SDL_PointInRect(&p,&e.rect)) speedCovered = true;
  if (Features::fastForward && SDL_PointInRect(&p, &this->speedRect) &&
      !speedCovered && !this->optionsOpen() && !this->filter.open)
    return "Game speed: " + std::to_string(this->selectedSpeed) + "x. Click for " +
           std::to_string(this->selectedSpeed == 1 ? 2 : this->selectedSpeed == 2 ? 5 : 1) + "x.";
  for (const Entry &e : this->entries)
    if (e.open && e.lockRect.w > 0 && SDL_PointInRect(&p, &e.lockRect))
      return e.locked ? "Locked: this window stays where it is. Click to let it be dragged again."
                      : "Lock this window where it is, so it can't be dragged by accident.";
  const UiElement *found = nullptr;
  // (buttons, and the images and status bars with help of their own: the
  // animal panel's icons, "Animal hunger. Assign zookeepers ..." and bars,
  // "Hunger: %d")
  std::function<void(const UiElement *)> search = [&](const UiElement *e) {
    if (e->isHidden())
      return;
    bool button = dynamic_cast<const UiButton *>(e) != nullptr;
    if (button || dynamic_cast<const UiImage *>(e) || dynamic_cast<const UiStatusImage *>(e)) {
      SDL_Rect r = e->getLastRect();
      int help = e->helpId();
      if (r.w > 0 && SDL_PointInRect(&p, &r) && help > 0 &&
          (button || !this->resource_manager->getString(30000 + help).empty()))
        found = e;
    }
    for (const UiElement *c : const_cast<UiElement *>(e)->getChildren())
      search(c);
  };
  for (auto it = this->entries.rbegin(); it != this->entries.rend() && !found; ++it)
    if (it->open && (!it->panel || SDL_PointInRect(&p, &it->rect)))
      search(it->layout);
  if (found) {
    // The view toggles say what a click does: "Click to hide foliage in
    // the game area." on, "Click to show ..." off
    int help = found->helpId();
    const UiButton *b = dynamic_cast<const UiButton *>(found);
    if (b && !b->isToggledOn()) {
      if (help == 1066)
        return this->resource_manager->getString(31065);
      if (help == 1067)
        return this->resource_manager->getString(31069);
      if (help == 1068)
        return this->resource_manager->getString(31070);
    }
    std::string tip = this->resource_manager->getString(30000 + help);
    // (a status bar's: its value - "Happiness: %d")
    if (const UiStatusImage *s = dynamic_cast<const UiStatusImage *>(found)) {
      size_t at = tip.find("%d");
      if (at != std::string::npos)
        tip.replace(at, 2, std::to_string(s->getValue()));
    }
    return tip;
  }
  if (this->isOverHud(this->mouseX, this->mouseY))
    return "";
  return this->worldTip;
}

// A box with a gold edge, the help in white outlined in black, wrapped,
// centred under the cursor and kept on the screen
void UiGameScreen::drawTooltip(SDL_Renderer *renderer, SDL_Rect *layout_rect) {
  // (measured: 600 ms still at the default delay; then shown for [UI]
  // tooltipDuration - zoo.exe 0x51a59f. The Tool Tip Delay slider keeps
  // [UI] tooltipDelay = value x 10 + 1 (0x51a559) in the UI manager - a
  // static object at 0x638de0: the slider writes its +0x20, 0x638e00, and
  // the duration its +0x24, 0x638e04; it reads both from [UI] TooltipDelay /
  // TooltipDuration at start, 0x51e3b8, defaults 1 and 3000 ms, 0x539a66.
  // It's added here as ms on top of the 600 - UNVERIFIED: the code that
  // waits on +0x20 is reached only through the manager's pointer and isn't
  // found yet, so its unit is a guess)
  this->shownTip = SDL_Rect{0, 0, 0, 0};
  Uint32 now = SDL_GetTicks();
  Uint32 wait = 600 + static_cast<Uint32>(std::max(0, this->tooltipDelayMs - 1));
  if (now < this->mouseStill + wait || now > this->mouseStill + wait + this->tooltipDurationMs)
    return;
  std::string text = this->tooltipText();
  if (text.empty())
    return;
  // Short or long (zoo.exe helpTypeSetting 0x442d19): the widest a tooltip's
  // line may be - [UI] maxShortTooltipWidth or maxLongTooltipWidth, as
  // [UI] helpType says (the UI manager's +0x164 / +0x168, 200 and 400 when
  // zoo.ini hasn't them: its constructor 0x539a66)
  const int font = 7002, size = 7003;
  const int maxWidth = Settings::getInt("UI", "helpType", 0) != 0 ? Settings::getInt("UI", "maxLongTooltipWidth", 400)
                                                                   : Settings::getInt("UI", "maxShortTooltipWidth", 200);
  ResourceManager *rm = this->resource_manager;
  auto width = [&](const std::string &s) {
    SDL_Texture *t = rm->getStringTexture(renderer, font, s, SDL_Color{255, 255, 255, 255}, size);
    int w = 0, h = 0;
    if (t)
      rm->getTextSize(t, &w, &h);
    return w;
  };
  std::vector<std::string> lines;
  std::string line, word;
  auto flush = [&] {
    if (word.empty())
      return;
    std::string tryLine = line.empty() ? word : line + " " + word;
    if (!line.empty() && width(tryLine) > maxWidth) {
      lines.push_back(line);
      line = word;
    } else {
      line = tryLine;
    }
    word.clear();
  };
  for (char ch : text) {
    if (ch == ' ')
      flush();
    else
      word += ch;
  }
  flush();
  if (!line.empty())
    lines.push_back(line);
  int w = 0;
  for (const std::string &l : lines)
    w = std::max(w, width(l));
  const int lineH = rm->getFontLineHeight(font, size);
  SDL_Rect box = {0, 0, w + 10, static_cast<int>(lines.size()) * lineH + 6};
  box.x = this->mouseX - box.w / 2;
  box.y = this->mouseY + 30;
  box.x = std::clamp(box.x, layout_rect->x, layout_rect->x + layout_rect->w - box.w);
  if (box.y + box.h > layout_rect->y + layout_rect->h)
    box.y = this->mouseY - 8 - box.h;
  this->shownTip = box;
  SDL_SetRenderDrawColor(renderer, 255, 227, 85, 255);
  ZT_RenderFillRect(renderer, &box);
  SDL_Rect inner = {box.x + 1, box.y + 1, box.w - 2, box.h - 2};
  SDL_SetRenderDrawColor(renderer, 82, 75, 43, 255);
  ZT_RenderFillRect(renderer, &inner);
  SDL_Rect fill = {box.x + 2, box.y + 2, box.w - 4, box.h - 4};
  SDL_SetRenderDrawColor(renderer, 115, 101, 49, 255);
  ZT_RenderFillRect(renderer, &fill);
  int y = box.y + 3;
  for (const std::string &l : lines) {
    SDL_Texture *black = rm->getStringTexture(renderer, font, l, SDL_Color{0, 0, 0, 255}, size);
    SDL_Texture *white = rm->getStringTexture(renderer, font, l, SDL_Color{255, 255, 255, 255}, size);
    int tw = 0, th = 0;
    if (white)
      rm->getTextSize(white, &tw, &th);
    int x = box.x + (box.w - tw) / 2;
    if (black) {
      const int off[4][2] = {{-1, 0}, {1, 0}, {0, -1}, {0, 1}};
      for (auto &o : off) {
        SDL_Rect d = {x + o[0], y + o[1], tw, th};
        ZT_RenderCopy(renderer, black, nullptr, &d);
      }
    }
    if (white) {
      SDL_Rect d = {x, y, tw, th};
      ZT_RenderCopy(renderer, white, nullptr, &d);
    }
    y += lineH;
  }
  // (the Lab's Play: the screen as drawn, tooltip and all - a window grab
  // of the GPU window came back without it)
  if (RenderSettings::uiPass != RenderSettings::UiPass::Art && Embed::active() && this->tooltipShot) {
    this->tooltipShot = false;
    SDL_Rect vp;
    SDL_GetRenderViewport(renderer, &vp);
    float sx = 1, sy = 1;
    SDL_GetRenderScale(renderer, &sx, &sy);
    int ow = 0, oh = 0;
    SDL_GetCurrentRenderOutputSize(renderer, &ow, &oh);
    if (SDL_Surface *s = SDL_CreateSurface(ow, oh, SDL_PIXELFORMAT_RGB24)) {
      SDL_SetRenderViewport(renderer, nullptr);
      SDL_SetRenderScale(renderer, 1, 1);
      if (ZT_RenderReadPixels(renderer, nullptr, SDL_PIXELFORMAT_RGB24, s->pixels, s->pitch) == 0)
        SDL_SaveBMP(s, "tooltip.bmp");
      SDL_SetRenderScale(renderer, sx, sy);
      SDL_SetRenderViewport(renderer, &vp);
      SDL_DestroySurface(s);
    }
  }
}

// zoo.exe 0x49c6d7 (the message bar's add, from messageBarShow 0x49ccc3 -
// postZooMessage 0x49ce6b's way in - and 0x49cec0, postUnitMessage
// 0x49d18e's; the message made by 0x49c96e with its kind, what it's about
// and the time): a kind 0-2 message whose text matches one kept from less than
// minimumMessageInterval seconds ago (by the clock, timeGetTime) is dropped;
// kind 3 and up always goes in; the newest goes first and the oldest is let
// go past 25 (+0x440, the bar's constructor 0x520a08)
void UiGameScreen::postMessage(const std::string &text, int kind, Subject subject, int id) {
  Uint32 now = SDL_GetTicks();
  if (kind < Original::messageRepeatKinds)
    for (const LoggedMessage &m : this->messageLog)
      if (m.text == text && now - m.at < static_cast<Uint32>(minimumMessageInterval) * 1000u)
        return;
  static const SDL_Color colours[3] = {{255, 255, 255, 255}, {0, 255, 0, 255}, {255, 0, 0, 255}};
  this->showMessage(text, colours[kind % 3]);
  this->messageLog.insert(this->messageLog.begin(), {text, kind, subject, id, now});
  if (this->messageLog.size() > static_cast<size_t>(Original::messagesKept))
    this->messageLog.resize(Original::messagesKept);
  this->messageSerial++;
}

void UiGameScreen::showMessage(const std::string &text, SDL_Color color, Uint32 ms) {
  this->message = text;
  this->messageColor = color;
  this->messageUntil = SDL_GetTicks() + ms;
}

// The bar's art is drawn with the screen (shown while there's a message);
// the message goes on it, centred (gamescrn.lyt: font 7002, size 7003)
// measured: opening the Message List sets the bar's +0x450 to 1 and closing
// it clears it (zoo.exe 0x4a03b4, 0x4a03fb); with it set the bar's draw
// (slot 0x2c, 0x4197fc) does nothing when +0x458 is set (never written for
// this class - stale heap, non-zero in the runs measured), so the bar isn't
// drawn while the list is open (the list shows it instead)
void UiGameScreen::drawMessage(SDL_Renderer *renderer, SDL_Rect *layout_rect) {
  if (!this->messageBar || this->messageBarHeld())
    return;
  std::string text;
  SDL_Color color = {255, 255, 255, 255};
  if (!this->message.empty() && SDL_GetTicks() < this->messageUntil) {
    text = this->message;
    color = this->messageColor;
  } else if (this->pausedMessage) {
    text = this->resource_manager->getString(400); // "The game is paused."
  }
  if (text.empty())
    return;
  // Centred across the screen (x=center)
  SDL_Rect where = *layout_rect;
  SDL_Rect r = this->messageBar->computeRect(renderer, layout_rect);
  where.x -= (r.x + r.w / 2) - (layout_rect->x + layout_rect->w / 2);
  this->messageBar->draw(renderer, &where);
  SDL_Rect bar = this->messageBar->getLastRect();
  // A message too long for the bar wraps onto more lines, each centred,
  // 25 px in from its ends (gamescrn.lyt [MessageText] xborder; as the
  // original: "Only show toys and animals that can do tricks can be / placed
  // in show tanks." on two lines in the bar)
  const int room = std::max(1, bar.w - 2 * 25);
  auto width = [&](const std::string &s) {
    int w = 0, h = 0;
    if (SDL_Texture *t = this->resource_manager->getStringTexture(renderer, 7002, s, color, 7003))
      this->resource_manager->getTextSize(t, &w, &h);
    return w;
  };
  std::vector<std::string> lines;
  {
    std::istringstream words(text);
    std::string word, line;
    while (words >> word) {
      const std::string candidate = line.empty() ? word : line + " " + word;
      if (!line.empty() && width(candidate) > room) {
        lines.push_back(line);
        line = word;
      } else {
        line = candidate;
      }
    }
    if (!line.empty())
      lines.push_back(line);
  }
  const int lineH = this->resource_manager->getFontLineHeight(7002, 7003);
  int y = bar.y + (bar.h - lineH * static_cast<int>(lines.size())) / 2;
  for (const std::string &line : lines) {
    SDL_Texture *t = this->resource_manager->getStringTexture(renderer, 7002, line, color, 7003);
    if (!t)
      continue;
    int w = 0, h = 0;
    this->resource_manager->getTextSize(t, &w, &h);
    SDL_Rect dst = {bar.x + (bar.w - w) / 2, y, w, h};
    ZT_RenderCopy(renderer, t, nullptr, &dst);
    y += lineH;
  }
}

// The filter list drops down over its button (xpac.lyt's x/y are in the
// panels' anchor, like the panels')
void UiGameScreen::drawPopup(SDL_Renderer *renderer, SDL_Rect *layout_rect) {
  if (!this->filter.open)
    return;
  const Entry *owner = nullptr;
  for (const Entry &e : this->entries)
    if (e.panel && e.open && e.id == this->filter.owner)
      owner = &e;
  // (its panel shut, the list goes with it: buya_Panel_hide 0x4e2cdd,
  // buyobj_Panel_hide 0x4e40d9, buldhab_Panel_hide 0x4e10d3 - their HUD
  // button let go, slot 0x54, and 0x5974_listC 0x4e0ee9(0): the list's
  // drop-down, uiFind 0x5974 +0x74, hidden; Build Exhibit's gate mode off
  // too, 0x4e1064(0) - gateMode)
  if (!owner) {
    this->filter.open = false;
    return;
  }
  SDL_Rect anchor = this->anchorRect(*owner, layout_rect);
  SDL_Rect r = this->filter.layout->computeRect(renderer, &anchor);
  this->filter.layout->draw(renderer, &r);
}

UiAction UiGameScreen::handleInputs(std::vector<Input> &inputs) {
  // Where the cursor rests (for tooltips): a move or a click starts the wait
  // again (the original shows a button's help while the cursor rests on it
  // after a click: seen side by side in the Lab's Play, the same clicks)
  for (const Input &in : inputs)
    if (in.type == InputType::POSITIONED) {
      if (in.event != InputEvent::CURSOR_MOVE ||
          in.position.x != this->mouseX || in.position.y != this->mouseY)
        this->mouseStill = SDL_GetTicks();
      this->mouseX = in.position.x;
      this->mouseY = in.position.y;
    }
  // A dialog takes every input while it is up
  if (this->dialog.layout) {
    this->handleDialog(inputs);
    return UiAction::NONE;
  }
  // So does the Options window (its Game page's Load, Save, Main Menu and
  // Exit come back as the original panel's do)
  // - but its own button on the sidebar (the disk) closes it again, as it
  // closes the original's panel
  if (this->optionsOpen()) {
    for (const Input &in : inputs) {
      if (in.event != InputEvent::LEFT_CLICK)
        continue;
      SDL_Point p = {in.position.x, in.position.y};
      for (UiButton *b : this->panelButtons) {
        SDL_Rect r = b->getLastRect();
        if (b->getActionTarget() == 5 && r.w > 0 && SDL_PointInRect(&p, &r)) {
          this->options->close();
          return UiAction::NONE;
        }
      }
    }
    return this->options->handleInputs(inputs);
  }
  // The filter list, while it is down, takes the mouse: a click picks a
  // choice, or (outside it) just closes it
  if (this->filter.open && this->filter.list) {
    int before = this->filter.list->getSelectedIndex();
    this->filter.layout->handleInputs(inputs);
    for (const Input &in : inputs) {
      if (in.event != InputEvent::LEFT_CLICK && in.event != InputEvent::RIGHT_CLICK)
        continue;
      int picked = this->filter.list->getSelectedIndex();
      this->filter.open = false;
      if (picked >= 0 && picked != before)
        this->setFilter(this->filter.owner, picked);
      break;
    }
    return UiAction::NONE;
  }

  // addition: consume the speed control's pointer events before native HUD
  // controls, dragging and map placement see them. Release remains captured
  // outside the button, so a press cannot finish as a placement or another click.
  if (!Features::fastForward) {
    this->speedDown = false;
    this->speedRect = {};
    this->selectedSpeed = 1;
  } else {
    for (Input &in : inputs) {
      if (in.type != InputType::POSITIONED) continue;
      const bool over = SDL_PointInRect(&in.position,&this->speedRect);
      const bool captured = this->speedDown;
      bool covered = false;
      for (const Entry &e : this->entries)
        if (e.panel && e.open && SDL_PointInRect(&in.position,&e.rect)) covered = true;
      if (over && !covered && in.event == InputEvent::LEFT_CLICK) {
        this->speedDown = true;
        this->speedPressUntil = SDL_GetTicks() + 160;
        this->selectedSpeed = this->selectedSpeed == 1 ? 2 : this->selectedSpeed == 2 ? 5 : 1;
        if (UiButton::clickSound) UiButton::clickSound();
        if (this->onGameSpeed) this->onGameSpeed(this->selectedSpeed);
      }
      if (in.event == InputEvent::LEFT_RELEASE) this->speedDown = false;
      if ((over && !covered) || (captured && (in.event == InputEvent::LEFT_RELEASE ||
          in.event == InputEvent::CURSOR_MOVE || in.event == InputEvent::MOUSE_MOVE))) {
        // addition: consumed events must still clear native hover selection.
        // A neutral move cannot click, drag or alter the real pointer/tooltip.
        Input outside{};
        outside.type = InputType::POSITIONED;
        outside.event = InputEvent::CURSOR_MOVE;
        outside.position = {-1,-1};
        outside.x = outside.y = -1;
        std::vector<Input> neutral{outside};
        this->hud->handleInputs(neutral);
        in.type = InputType::NONE;
        in.event = InputEvent::NONE;
      }
    }
  }

  // A window dragged by its title (or the original's handle): it follows
  // the mouse once it has moved a few pixels; let go, it stays (with the
  // dragwindows extra, next time too: zoo.ini [Windows])
  for (const Input &in : inputs) {
    int x = in.position.x, y = in.position.y;
    if (in.event == InputEvent::LEFT_CLICK && this->dragId < 0) {
      this->beginDrag(x, y);
    } else if (in.event == InputEvent::CURSOR_MOVE && this->dragId >= 0) {
      if (!this->dragMoving && (std::abs(x - this->dragFromX) > 3 || std::abs(y - this->dragFromY) > 3))
        this->dragMoving = true;
      if (this->dragMoving)
        for (Entry &e : this->entries)
          if (e.panel && e.id == this->dragId) {
            e.moved = true;
            e.moveX = x - this->dragGrabX;
            e.moveY = y - this->dragGrabY;
          }
    } else if (in.event == InputEvent::LEFT_RELEASE && this->lockDownId >= 0) {
      // (a lock let go on: opened or shut)
      for (Entry &e : this->entries)
        if (e.panel && e.id == this->lockDownId && e.lockRect.w > 0 && SDL_PointInRect(&in.position, &e.lockRect)) {
          e.locked = !e.locked;
          Settings::set("Windows", "l" + std::to_string(e.id), e.locked ? "1" : "0");
        }
      this->lockDownId = -1;
      return UiAction::NONE;
    } else if (in.event == InputEvent::LEFT_RELEASE && this->dragId >= 0) {
      if (this->dragMoving && Features::dragWindows)
        for (const Entry &e : this->entries)
          if (e.panel && e.id == this->dragId)
            Settings::set("Windows", "w" + std::to_string(e.id), std::to_string(e.rect.x) + "," + std::to_string(e.rect.y));
      bool wasMoving = this->dragMoving;
      this->dragId = -1;
      this->dragMoving = false;
      if (wasMoving)
        return UiAction::NONE;
    }
  }
  if (this->dragMoving)
    return UiAction::NONE;

  UiAction action = UiAction::NONE;
  if (this->pendingAction != UiAction::NONE) {
    action = this->pendingAction;
    this->pendingAction = UiAction::NONE;
    return action;
  }
  // Top-most first
  for (auto it = this->entries.rbegin(); it != this->entries.rend(); ++it) {
    if (!it->open)
      continue;
    UiAction a = it->layout->handleInputs(inputs);
    if (this->showSelection(a)) a = UiAction::NONE;
    if (static_cast<int>(a) == 152 && this->filter.list) {
      this->openFilter(it->id);
      a = UiAction::NONE;
    }
    // "Close the panel I'm in"
    if (isPanelClose(a) && panelOf(a) == kPanelSelf)
      a = (UiAction)(kPanelCloseAction + it->id);
    if (a != UiAction::NONE && action == UiAction::NONE)
      action = a;
  }

  // A tab may have changed what a buy panel lists
  for (BuyPanel &p : this->buyPanels)
    this->refreshBuyPanel(p);
  this->refreshStaffPlaque();
  this->refreshAdoptPlaque();
  this->refreshTerraform();
  this->refreshResearch();
  this->refreshZooStatus();
  this->refreshExhibits();
  this->refreshShow();
  this->refreshFilterPanel();
  this->refreshStaff();
  this->refreshAnimal();
  this->refreshGuest();
  this->refreshBuilding();
  this->refreshHelp();
  this->refreshLists();

  if (isPanelToggle(action)) {
    // The panel buttons are toggles in one radio set: each open panel is
    // the one whose button is on. Ours, with smartstack: only the sidebar's
    // panels (the buy panels, research, Game Options...: one spot, on the
    // left) replace one another; any other window opened stays, moved aside
    // if it would cover the new one
    const int clicked = panelOf(action);
    // (Game Options too - as the original's side panel; as the Options
    // window it's a window of its own and closes nothing)
    auto leftSide = [](int id) {
      return id == 3 || id == 4 || id == 8 || id == 10 || id == 15 || (id == 5 && !Features::optionsWindow) ||
             id == 803 || id == 34;
    };
    for (UiButton *b : this->panelButtons) {
      int t = b->getActionTarget();
      if (Features::smartStack && t != clicked && !b->isToggledOn() && this->isPanelOpen(t) &&
          !(leftSide(t) && leftSide(clicked))) {
        b->setToggledOn(true);
        continue;
      }
      this->setPanelOpen(t, b->isToggledOn());
    }
  } else if (isPanelClose(action)) {
    // (closed by its own button: its HUD button let go - as the panels'
    // Panel_hide do: Research's, research_Panel_hide 0x4a02fc: uiFind
    // 0x3fb, slot 0x54; Zoo Status', zooinfo_Panel_hide 0x4efff8: 0x3f6)
    int id = panelOf(action);
    this->setPanelOpen(id, false);
    for (UiButton *b : this->panelButtons)
      if (b->getActionTarget() == id)
        b->setToggledOn(false);
  }
  return action;
}

// ----------------------------------------------------------------------------
// The map under the mouse, the fence tool and the bulldozer
// ----------------------------------------------------------------------------
bool UiGameScreen::isOverHud(int x, int y) const {
  if (this->dialog.layout || this->filter.open || this->optionsOpen())
    return true;
  if (this->isOverPanel(x, y))
    return true;
  SDL_Point p = {x, y};
  // The toolbar column and the bottom bar's pieces (main.lyt's root images)
  for (int id = 1032; id <= 1039; id++)
    if (UiElement *e = this->hud ? this->hud->getElementById(id) : nullptr) {
      SDL_Rect r = e->getLastRect();
      if (SDL_PointInRect(&p, &r))
        return true;
    }
  return false;
}

std::string UiGameScreen::fenceToolFile() const {
  if (!this->isPanelOpen(8))
    return "";
  for (const BuyPanel &p : this->buyPanels) {
    if (p.id != 8 || p.category != "fence" || !p.region)
      continue;
    int i = p.region->getSelected();
    if (i >= 0 && i < (int)p.items.size())
      return p.items[i]->file;
  }
  return "";
}

std::string UiGameScreen::pathToolFile() const {
  if (!this->isPanelOpen(8))
    return "";
  for (const BuyPanel &p : this->buyPanels) {
    if (p.id != 8 || p.category != "paths" || !p.region)
      continue;
    int i = p.region->getSelected();
    if (i >= 0 && i < (int)p.items.size())
      return p.items[i]->file;
  }
  return "";
}

// The staff member held comes from Hire Staff's grid while it's shown
// (zoo.exe personel_Staff_show 0x4e6f87: the zoo view's +0x364 set to the
// grid, 0xe12; personel_Staff_hide 0x4e6794: back to none, and nothing
// held, setPlaceItem 0x4f189a)
// zoo.exe 0xe5c_click 0x618862 / 0xe5d_click 0x618883: 0x63e46c one back
// (0 to 3) or on (3 to 0), the list refilled facing that way (0x4e6bbd ->
// 0x4e90c2) and the pick made again (personel_Staff_click 0x4e69b8), which
// hands it to the zoo view (0x4df56d: +0x370) - here, the panel's facing
// (rotateItems), read by the staff tool
int UiGameScreen::staffToolFacing() const {
  for (const BuyPanel &p : this->buyPanels)
    if (p.id == 10)
      return p.facing;
  return 0;
}

std::string UiGameScreen::staffToolFile() const {
  if (!this->isPanelOpen(10))
    return "";
  for (const BuyPanel &p : this->buyPanels) {
    if (p.id != 10 || !p.region)
      continue;
    int i = p.region->getSelected();
    if (i >= 0 && i < (int)p.items.size())
      return p.items[i]->file;
  }
  return "";
}

void UiGameScreen::dropPicks() {
  this->animalPick.clear();
  this->setGateMode(false);
  // (fences and paths too - measured: the original's right click over the
  // map lets the fence or path picked go, its panel left open; Lab's Play
  // original, 2026-11-11)
  static const char *placed[] = {"animals", "shelters", "toys", "showtoys", "structures", "scenery", "foliage", "rocks",
                                 "fence", "paths"};
  for (BuyPanel &p : this->buyPanels) {
    bool drop = false;
    for (const char *c : placed)
      drop = drop || p.category == c;
    if (p.id == 10) {
      // (the staff member held too: the zoo view's placing let go, and
      // Hire Staff's pick with it - personel_Staff_off 0x4e6711; measured:
      // the right click leaves no pick, no name, salary or duties, the
      // info plaque button greyed, the plaque itself left up)
      this->hireStaffLetGo(p);
      continue;
    }
    if (!drop || !p.region || p.region->getSelected() < 0)
      continue;
    p.region->setSelected(-1);
    this->showItem(p, -1);
  }
}

// The object held comes from its panel's list while that's shown (zoo.exe
// buyobj_Objects_show 0x4e48be, buldhab_Objects_show 0x4e1091: the zoo
// view's +0x364 set to the list, 0xbcb or 0xc88; buyobj_Objects_hide
// 0x4e4012, buldhab_Objects_hide 0x4e1079: back to none, nothing held)
std::string UiGameScreen::objectToolFile(int &cost, int &facing) const {
  cost = 0;
  facing = 0;
  static const char *placed[] = {"shelters", "toys", "showtoys", "structures", "scenery", "foliage", "rocks"};
  for (const BuyPanel &p : this->buyPanels) {
    if (!this->isPanelOpen(p.id) || !p.region)
      continue;
    bool objects = false;
    for (const char *c : placed)
      objects = objects || p.category == c;
    if (!objects)
      continue;
    int i = p.region->getSelected();
    if (i >= 0 && i < (int)p.items.size()) {
      cost = p.items[i]->cost;
      facing = p.facing;
      return p.items[i]->file;
    }
  }
  return "";
}

// The animal held comes from Adopt's Animals list while it's shown
// (zoo.exe buya_Animals_show 0x4e2bda: the zoo view's +0x364 set to the
// list, 0x7e3; buya_Animals_hide 0x4e2ba6: back to none, nothing held;
// buya_Animals_off 0x4e2bb5, the Animals tab let go: setPlaceItem 0x4f189a
// with none - nothing held)
std::string UiGameScreen::animalToolFile(bool &female) const {
  female = this->animalPickFemale;
  if (!this->isPanelOpen(3))
    return "";
  for (const BuyPanel &p : this->buyPanels)
    if (p.id == 3 && p.category == "animals")
      return this->animalPick;
  return "";
}

// zoo.exe buya_RotateRightButton_click 0x48e40f / buya_RotateLeftButton_click
// 0x618c9c turn 0x63bb50 (0 to 3; 0 at each zoo, 0x4f0a84), the panel's
// one facing for all its tabs; buya_Animals_click 0x4e2cf8 hands it to the
// zoo view (0x4df56d: +0x370) with the animal
int UiGameScreen::animalToolFacing() const {
  for (const BuyPanel &p : this->buyPanels)
    if (p.id == 3)
      return p.facing;
  return 0;
}

bool UiGameScreen::viewShown(int id) const {
  UiButton *b = this->hud ? dynamic_cast<UiButton *>(this->hud->getElementById(id)) : nullptr;
  return !b || b->isToggledOn();
}

// (picking something from a list puts the bulldozer away first)
void UiGameScreen::bulldozerOff() {
  if (UiButton *b = this->hud ? dynamic_cast<UiButton *>(this->hud->getElementById(1028)) : nullptr)
    b->setToggledOn(false);
}

// The bulldozer button (Clear_Objects 1028, a toggle) on: the zoo view
// bulldozes (zoo.exe main_ClearObjects_click 0x509722 -> 0x5096e8(on))
bool UiGameScreen::bulldozerOn() const {
  UiButton *b = this->hud ? dynamic_cast<UiButton *>(this->hud->getElementById(1028)) : nullptr;
  return b && b->isToggledOn();
}

// ----------------------------------------------------------------------------
// Dialogs
// ----------------------------------------------------------------------------
void UiGameScreen::closeDialog() {
  if (this->dialog.layout && this->dialog.textId)
    if (UiText *t = dynamic_cast<UiText *>(this->dialog.layout->getElementById(this->dialog.textId)))
      if (t->isBeingEdited())
        t->endEdit(false);
  delete this->dialog.layout;
  this->dialog = Dialog{};
  if (!this->dialogQueue.empty()) {
    auto next = this->dialogQueue.front();
    this->dialogQueue.erase(this->dialogQueue.begin());
    next();
  }
}

void UiGameScreen::askName(const std::string &prompt, const std::string &initial,
                           std::function<void(const std::string &)> done) {
  if (this->dialog.layout) {
    this->dialogQueue.push_back([=] { this->askName(prompt, initial, done); });
    return;
  }
  IniReader *ini = this->resource_manager->getIniReader("ui/newname.lyt");
  if (!ini)
    return;
  this->dialog.layout = new UiLayout(ini, this->resource_manager);
  this->dialog.textId = 45308;
  this->dialog.typed = initial;
  this->dialog.done = done;
  UiLayout *d = this->dialog.layout;
  if (UiText *m = dynamic_cast<UiText *>(d->getElementById(45306)))
    m->setText(prompt);
  // Just OK (the original's name box)
  for (int id : {45310, 45311, 45313})
    if (UiElement *e = d->getElementById(id))
      e->setHidden(true);
  if (UiButton *ok = dynamic_cast<UiButton *>(d->getElementById(45312)))
    ok->onClick = [this] { this->dialog.submit = true; };
  if (UiText *t = dynamic_cast<UiText *>(d->getElementById(45308))) {
    t->setInputBox(true);
    t->setText(initial);
    t->onCommit = [this](const std::string &typed) { this->dialog.typed = typed; };
    t->beginEdit();
  }
}

void UiGameScreen::tell(const std::string &message) {
  if (this->dialog.layout) {
    this->dialogQueue.push_back([=] { this->tell(message); });
    return;
  }
  IniReader *ini = this->resource_manager->getIniReader("ui/confirm.lyt");
  if (!ini)
    return;
  this->dialog.layout = new UiLayout(ini, this->resource_manager);
  this->dialog.yes = nullptr;
  UiLayout *d = this->dialog.layout;
  if (UiText *m = dynamic_cast<UiText *>(d->getElementById(45006)))
    m->setMessage(message);
  if (UiImage *icon = dynamic_cast<UiImage *>(d->getElementById(45002)))
    icon->setImage("ui/sharedui/exclaim/exclaim");
  for (int id : {45010, 45011, 45013}) // just OK
    if (UiElement *e = d->getElementById(id))
      e->setHidden(true);
  if (UiButton *ok = dynamic_cast<UiButton *>(d->getElementById(45012)))
    ok->onClick = [this] { this->dialog.submit = true; };
}

void UiGameScreen::popup(const std::string &image, const std::string &message) {
  ScenarioPopup spec;
  spec.image1 = image;
  spec.text = message;
  this->popup(spec);
}

// zoo.exe loadScenarioPopup 0x5a1b15: the popup's layout is its layout= key
// (the tutorials' 45100 confirml.lyt, a time limit's or an award's 45200
// confirmm.lyt, 45000 confirm.lyt); each part is the element the layout's id
// + 1 (background), + 2 (icon), + 4 (image 1), + 5 (image 2) and + 6 (the
// message) say, set when the key gives one; OK alone
void UiGameScreen::popup(const ScenarioPopup &spec) {
  if (this->dialog.layout) {
    this->dialogQueue.push_back([=] { this->popup(spec); });
    return;
  }
  const int base = spec.layout == 45100 || spec.layout == 45000 ? spec.layout : 45200;
  IniReader *ini = this->resource_manager->getIniReader(base == 45100   ? "ui/confirml.lyt"
                                                        : base == 45000 ? "ui/confirm.lyt"
                                                                        : "ui/confirmm.lyt");
  if (!ini)
    return;
  this->dialog.layout = new UiLayout(ini, this->resource_manager);
  this->dialog.yes = nullptr;
  UiLayout *d = this->dialog.layout;
  if (UiText *m = dynamic_cast<UiText *>(d->getElementById(base + 6)))
    m->setMessage(spec.text);
  auto picture = [&](int id, const std::string &art) {
    if (art.empty())
      return;
    if (UiImage *img = dynamic_cast<UiImage *>(d->getElementById(id)))
      img->setImage(art);
  };
  picture(base + 1, spec.background);
  picture(base + 2, spec.icon);
  picture(base + 4, spec.image1);
  picture(base + 5, spec.image2);
  for (int id : {base + 10, base + 11, base + 13}) // just OK
    if (UiElement *e = d->getElementById(id))
      e->setHidden(true);
  if (UiButton *ok = dynamic_cast<UiButton *>(d->getElementById(base + 12)))
    ok->onClick = [this] { this->dialog.submit = true; };
}

int UiGameScreen::listSelection(int id) {
  UiListBox *list = id == 22900 && this->filter.list ? this->filter.list : dynamic_cast<UiListBox *>(this->getElementById(id));
  if (!list)
    return -1;
  return list->getSelectedIndex();
}

std::string UiGameScreen::listText(int id) {
  UiListBox *list = id == 22900 && this->filter.list ? this->filter.list : dynamic_cast<UiListBox *>(this->getElementById(id));
  return list ? list->getSelectedText() : "";
}

UiAction UiGameScreen::clickElement(int id) {
  UiElement *e = this->getElementById(id);
  if (!e || e->isHidden())
    return UiAction::NONE;
  SDL_Rect r = e->getLastRect();
  Input in{};
  in.type = InputType::POSITIONED;
  in.event = InputEvent::LEFT_CLICK;
  in.position = {r.x + r.w / 2, r.y + r.h / 2};
  in.x = in.position.x;
  in.y = in.position.y;
  std::vector<Input> inputs = {in};
  this->handleInputs(inputs);
  std::vector<Input> none;
  return this->handleInputs(none); // the action it asked for
}

std::string UiGameScreen::dialogMessage() const {
  if (!this->dialog.layout)
    return "";
  for (int id : {45206, 45106, 45006})
    if (UiText *m = dynamic_cast<UiText *>(this->dialog.layout->getElementById(id)))
      return m->getText();
  return "";
}

void UiGameScreen::answerDialog(bool yes) {
  if (!this->dialog.layout)
    return;
  (yes ? this->dialog.submit : this->dialog.cancel) = true;
  std::vector<Input> none;
  this->handleDialog(none);
}

void UiGameScreen::popupYesNo(const std::string &message, std::function<void()> yes, std::function<void()> no) {
  if (this->dialog.layout) {
    this->dialogQueue.push_back([=] { this->popupYesNo(message, yes, no); });
    return;
  }
  IniReader *ini = this->resource_manager->getIniReader("ui/confirmm.lyt");
  if (!ini)
    return;
  // (Yes and No at their altx / alty, as askConfirm's)
  for (const char *b : {"yes", "no"}) {
    std::string ax = ini->get(b, "altx"), ay = ini->get(b, "alty");
    if (!ax.empty())
      ini->set(b, "x", ax);
    if (!ay.empty())
      ini->set(b, "y", ay);
  }
  this->dialog.layout = new UiLayout(ini, this->resource_manager);
  this->dialog.yes = yes;
  this->dialog.no = no;
  UiLayout *d = this->dialog.layout;
  if (UiText *m = dynamic_cast<UiText *>(d->getElementById(45206)))
    m->setMessage(message);
  for (int id : {45212, 45213}) // OK, Cancel
    if (UiElement *e = d->getElementById(id))
      e->setHidden(true);
  if (UiButton *y = dynamic_cast<UiButton *>(d->getElementById(45210)))
    y->onClick = [this] { this->dialog.submit = true; };
  if (UiButton *n = dynamic_cast<UiButton *>(d->getElementById(45211)))
    n->onClick = [this] { this->dialog.cancel = true; };
}

// zoo.exe showDialog 0x4fff2c (no text pointer: base [0x63b950] 45000,
// confirm.lyt; the message by text number, the picture given - the question
// - and the don't-show-again box, 45014/45015, hidden) -> 0x4ff63c style 3:
// every button hidden, then Cancel shown at its x / y and Yes and No at
// theirs (not their altx / alty); the game paused while it's up
void UiGameScreen::askYesNoCancel(const std::string &message, std::function<void()> yes, std::function<void()> no) {
  if (this->dialog.layout) {
    this->dialogQueue.push_back([=] { this->askYesNoCancel(message, yes, no); });
    return;
  }
  IniReader *ini = this->resource_manager->getIniReader("ui/confirm.lyt");
  if (!ini)
    return;
  this->dialog.layout = new UiLayout(ini, this->resource_manager);
  this->dialog.yes = yes;
  this->dialog.no = no;
  UiLayout *d = this->dialog.layout;
  if (UiText *m = dynamic_cast<UiText *>(d->getElementById(45006)))
    m->setMessage(message);
  if (UiImage *icon = dynamic_cast<UiImage *>(d->getElementById(45002)))
    icon->setImage("ui/sharedui/question/question");
  if (UiElement *e = d->getElementById(45012)) // OK
    e->setHidden(true);
  if (UiButton *y = dynamic_cast<UiButton *>(d->getElementById(45010)))
    y->onClick = [this] { this->dialog.submit = true; };
  if (UiButton *n = dynamic_cast<UiButton *>(d->getElementById(45011)))
    n->onClick = [this] { this->dialog.cancel = true; };
  if (UiButton *c = dynamic_cast<UiButton *>(d->getElementById(45013)))
    c->onClick = [this] { this->dialog.dismiss = true; };
}

UiElement *UiGameScreen::dialogElement(int id) const {
  return this->dialog.layout ? this->dialog.layout->getElementById(id) : nullptr;
}

void UiGameScreen::cancelDialog() {
  if (!this->dialog.layout)
    return;
  this->dialog.dismiss = true;
  std::vector<Input> none;
  this->handleDialog(none);
}

void UiGameScreen::askConfirm(const std::string &message, std::function<void()> yes) {
  if (this->dialog.layout) {
    this->dialogQueue.push_back([=] { this->askConfirm(message, yes); });
    return;
  }
  IniReader *ini = this->resource_manager->getIniReader("ui/confirm.lyt");
  if (!ini)
    return;
  // Two buttons, Yes and No: each at its altx / alty, the pair centred under
  // the message (the original's: its x / y are the four-button row's)
  for (const char *b : {"yes", "no"}) {
    std::string ax = ini->get(b, "altx"), ay = ini->get(b, "alty");
    if (!ax.empty())
      ini->set(b, "x", ax);
    if (!ay.empty())
      ini->set(b, "y", ay);
  }
  this->dialog.layout = new UiLayout(ini, this->resource_manager);
  this->dialog.yes = yes;
  UiLayout *d = this->dialog.layout;
  if (UiText *m = dynamic_cast<UiText *>(d->getElementById(45006)))
    m->setMessage(message);
  for (int id : {45012, 45013}) // OK, Cancel
    if (UiElement *e = d->getElementById(id))
      e->setHidden(true);
  if (UiButton *y = dynamic_cast<UiButton *>(d->getElementById(45010)))
    y->onClick = [this] { this->dialog.submit = true; };
  if (UiButton *n = dynamic_cast<UiButton *>(d->getElementById(45011)))
    n->onClick = [this] { this->dialog.cancel = true; };
}

void UiGameScreen::handleDialog(std::vector<Input> &inputs) {
  this->dialog.layout->handleInputs(inputs);
  for (const Input &in : inputs)
    if (in.event == InputEvent::KEY_DOWN && (in.key == SDLK_RETURN || in.key == SDLK_KP_ENTER))
      this->dialog.submit = true;
  if (this->dialog.dismiss) {
    this->closeDialog();
    return;
  }
  if (this->dialog.cancel) {
    auto no = this->dialog.no;
    this->closeDialog();
    if (no)
      no();
    return;
  }
  if (!this->dialog.submit)
    return;
  if (this->dialog.textId)
    if (UiText *t = dynamic_cast<UiText *>(this->dialog.layout->getElementById(this->dialog.textId))) {
      if (t->isBeingEdited())
        t->endEdit(true); // (onCommit takes the text)
      else
        this->dialog.typed = t->getText();
    }
  auto done = this->dialog.done;
  auto yes = this->dialog.yes;
  std::string typed = this->dialog.typed;
  this->dialog.submit = false;
  delete this->dialog.layout;
  this->dialog = Dialog{};
  if (done)
    done(typed);
  if (yes)
    yes();
  if (!this->dialog.layout && !this->dialogQueue.empty()) {
    auto next = this->dialogQueue.front();
    this->dialogQueue.erase(this->dialogQueue.begin());
    next();
  }
}

void UiGameScreen::drawDialog(SDL_Renderer *renderer, SDL_Rect *layout_rect) {
  if (!this->dialog.layout)
    return;
  SDL_Rect r = this->dialog.layout->computeRect(renderer, layout_rect);
  this->dialog.layout->draw(renderer, &r);
}

// ----------------------------------------------------------------------------
// Exhibit/Show List (mulhab.lyt, id 30): measured against the original
// after fencing one exhibit: the list holds its name; picking it centres
// the map on it and shows its name at the top and its information: when it
// was constructed ("Jun 22, Year 1": lang string 22014 MMM d', Year' y),
// upkeep last / current / total ($0.00) and whether staff are assigned
// (No). A tank's page starts on Modify Tank (tankmod.lyt); other exhibits'
// on their status, with Modify Tank greyed.
// ----------------------------------------------------------------------------
void UiGameScreen::setupExhibits() {
  for (Entry &e : this->entries)
    if (e.panel && e.id == 30)
      this->exhibitPanel = e.layout;
  if (!this->exhibitPanel)
    return;
  UiLayout *z = this->exhibitPanel;
  if (UiText *name = dynamic_cast<UiText *>(z->getElementById(4304)))
    name->onCommit = [this](const std::string &typed) {
      // (zoo.exe mulhab_HabitatName_click 0x6107ef -> 0x5aedbd: kept only
      // with something to see in it; its name cheats, 0x5aee66, run first)
      if (!this->fences || !Original::nameTaken(typed))
        return;
      if (this->exhibitRenamed)
        this->exhibitRenamed(typed);
      if (Fences::Exhibit *ex = this->fences->exhibit(this->shownExhibit))
        ex->name = typed;
      this->shownListCount = -1; // relist
    };
  if (UiButton *drain = dynamic_cast<UiButton *>(z->getElementById(4885)))
    drain->onClick = [this] {
      if (!this->fences)
        return;
      // (zoo.exe tankDrainClick 0x611487 -> tankDrainCheck 0x6064ed:
      // animals in it, 10551 and nothing done)
      // (and only with a gate to the tank, 0x410349: without, nothing is said)
      if (const int refusal = this->fences->drainRefusal(this->shownExhibit)) {
        if (refusal == 1)
          this->showMessage(this->resource_manager->getString(10551));
        return;
      }
      this->fences->drainExhibit(this->shownExhibit);
    };
  for (int id : {4869, 4870}) // salt, fresh: fill it again, for the water
    if (UiButton *fill = dynamic_cast<UiButton *>(z->getElementById(id)))
      fill->onClick = [this, id] {
        if (!this->fences)
          return;
        bool salt = id == 4869;
        int cost = this->fences->fillCost(this->shownExhibit, salt);
        // (more than the zoo has: 10002, nothing done - zoo.exe 0x6115f2)
        if (this->sim && !this->sim->canAfford(cost)) {
          this->showMessage(this->resource_manager->getString(10002));
          return;
        }
        this->fences->fillExhibit(this->shownExhibit, salt);
        if (this->sim && cost > 0)
          this->sim->spend(ZooSim::Construction, cost);
      };
  // The wall down / up, the base recessed / exposed: a step each click
  const struct {
    int id, step;
    bool wall;
  } steps[] = {{4874, -1, true}, {4875, +1, true}, {4879, -1, false}, {4880, +1, false}};
  for (const auto &s : steps)
    if (UiButton *b = dynamic_cast<UiButton *>(z->getElementById(s.id)))
      b->onClick = [this, s] {
        if (!this->fences)
          return;
        int id = this->shownExhibit;
        bool can = s.wall ? this->fences->canAdjustWall(id, s.step)
                          : this->fences->canAdjustBase(id, s.step);
        if (!can)
          return;
        // (the wall: zoo.exe tankModBuy 0x4691cd prices it from its height,
        // 0x4ef687 - up to initialHeight free, down from above it money back
        // - and books that float as it is; the base: tankBaseBuy 0x46937e,
        // the step's price either way)
        const Fences::Exhibit *ex = this->fences->exhibit(id);
        double cost = s.wall ? (ex ? this->fences->wallChangeCost(id, ex->wallSub, ex->wallSub + s.step) : 0.0)
                             : this->fences->baseStepCost(id);
        // (more than the zoo has: 10002, nothing done - zoo.exe 0x4691cd
        // the wall, 0x46937e the base)
        if (this->sim && !this->sim->canAfford(cost)) {
          this->showMessage(this->resource_manager->getString(10002));
          return;
        }
        if (s.wall)
          this->fences->adjustWall(id, s.step);
        else
          this->fences->adjustBase(id, s.step);
        if (this->sim && cost != 0)
          this->sim->spend(ZooSim::Construction, cost);
      };
}

void UiGameScreen::refreshCatalog() {
  for (BuyPanel &p : this->buyPanels) {
    std::string picked;
    int sel = p.region ? p.region->getSelected() : -1;
    if (sel >= 0 && sel < (int)p.items.size())
      picked = p.items[sel]->file;
    int row = p.region ? p.region->getScrollPosition() : 0;
    p.category.clear();
    this->refreshBuyPanel(p);
    for (int i = 0; i < (int)p.items.size(); i++)
      if (p.items[i]->file == picked) {
        p.region->setSelected(i);
        this->showItem(p, i);
      }
    if (p.region)
      p.region->setScrollPosition(row);
  }
}

int UiGameScreen::scrollBuyPanel(int panelId, int row) {
  for (BuyPanel &p : this->buyPanels)
    if (p.id == panelId && p.region) {
      p.region->setScrollPosition(row);
      return p.region->getScrollMaximum();
    }
  return 0;
}

// ----------------------------------------------------------------------------
// A tank filter's panel (filter.lyt, its pages filter1 / filter2). Status:
// "Maximum efficiency" until it has decayed, then "Minimum efficiency"; its
// last service ("%s month(s) ago": none yet, so since it went in) and its
// months in operation. Finance: upkeep last / current / total. Sell gives
// back half its price.
// ----------------------------------------------------------------------------
void UiGameScreen::setupFilterPanel() {
  for (Entry &e : this->entries)
    if (e.panel && e.id == 46)
      this->filterPanel = e.layout;
  if (!this->filterPanel)
    return;
  UiLayout *z = this->filterPanel;
  if (UiText *name = dynamic_cast<UiText *>(z->getElementById(4904)))
    name->onCommit = [this](const std::string &typed) {
      // (zoo.exe filter_BuildingName_click 0x616076: setName, 0x449215)
      if (!this->fences || !Original::nameTaken(typed))
        return;
      if (Fences::Filter *f = this->fences->filter(this->shownFilter))
        f->name = typed;
    };
  if (UiButton *sell = dynamic_cast<UiButton *>(z->getElementById(4906)))
    sell->onClick = [this] {
      if (!this->fences || !this->fences->filter(this->shownFilter))
        return;
      // (zoo.exe filter_SellFilter_click 0x616035: the zoo view's selected
      // object, if any, sold - sellSelected 0x48d052 -> sellObject 0x50a432:
      // its sell value, ZTTankFilter::sellValue 0x50c961, asked first - filter1.ai's
      // cNeedsConfirm - and booked on Recycling Benefit, bookRemovalRefund
      // 0x4f9329)
      const Fences::Filter *f = this->fences->filter(this->shownFilter);
      const int x = f->x, y = f->y;
      auto sell = [this, x, y] {
        if (!this->fences)
          return;
        for (size_t i = 0; i < this->fences->filters().size(); i++)
          if (this->fences->filters()[i].x == x && this->fences->filters()[i].y == y) {
            float refund = this->fences->sellValue(static_cast<int>(i));
            this->fences->removeFilter(static_cast<int>(i));
            if (this->sim && refund > 0)
              this->sim->earn(ZooSim::Recycling, refund);
            break;
          }
        this->shownFilter = -1;
        this->setPanelOpen(46, false);
      };
      if (Original::sellNeedsConfirm(this->resource_manager, f->file))
        this->askConfirm(Original::sellQuestion(this->resource_manager, f->name, this->fences->sellValue(this->shownFilter)), sell);
      else
        sell();
    };
}

void UiGameScreen::showFilter(int index) {
  if (!this->filterPanel)
    return;
  this->showPanel(0, "");
  this->setPanelOpen(46, true);
  this->shownFilter = index;
  if (UiButton *status = dynamic_cast<UiButton *>(this->filterPanel->getElementById(4909)))
    status->choose();
  this->filterPanel->syncTabs();
  this->refreshFilterPanel();
}

void UiGameScreen::refreshFilterPanel() {
  UiLayout *z = this->filterPanel;
  if (!z || !this->fences || !this->isPanelOpen(46))
    return;
  const Fences::Filter *f = this->fences->filter(this->shownFilter);
  if (!f) {
    this->setPanelOpen(46, false);
    return;
  }
  const Fences::FilterType &k = this->fences->filterType(f->file);
  auto text = [&](int eid, const std::string &s) {
    if (UiText *t = dynamic_cast<UiText *>(z->getElementById(eid)))
      if (!t->isBeingEdited())
        t->setText(s);
  };
  // (zoo.exe filter_Panel_show 0x615cce -> 0x615d28: the upkeep lines its
  // whole-dollar counters, +0x168 on, through moneyToText 0x40eca1 without
  // grouping or cents; the state "Broken Down" (4999) at no health,
  // 0x4113c9, "Minimum efficiency" (4914) at its decayed health or less,
  // 0x499148, else "Maximum efficiency" (4913))
  auto money = [](double v) { return formatCost(static_cast<int>(v)); };
  text(4904, f->name);
  if (UiImage *pic = dynamic_cast<UiImage *>(z->getElementById(4903)))
    pic->setImage("objects/filter/off/off");
  const int health = static_cast<int>(f->health);
  text(4913, this->resource_manager->getString(health <= 0 ? 4999 : health <= k.decayedHealth ? 4914 : 4913));
  // (filterPanelNumbers 0x615d28: "Last Filter Service: %s month(s) ago"
  // from the day it was last serviced, +0x180 - whole days + 29.9, cut,
  // / 30, no minimum; "Months in operation:" its +0x188)
  const double today = this->sim ? this->sim->today() : this->fences->today;
  const int monthCount = this->fences->month();
  const double serviced = this->sim ? this->sim->openedDayOf(f->servicedDay, f->placedMonth, monthCount) : f->servicedDay;
  int months = std::max(0, Original::monthsSince(serviced, today));
  std::string ago = this->resource_manager->getString(4916);
  size_t at = ago.find("%s");
  if (at != std::string::npos)
    ago.replace(at, 2, std::to_string(months));
  text(4916, ago);
  text(4918, std::to_string(f->monthsRunning));
  text(4921, money(f->upkeepLast));
  text(4923, money(f->upkeepCurrent));
  text(4925, money(f->upkeepTotal));
}

// ----------------------------------------------------------------------------
// Staff (measured in the original): a staff member's panel shows its name
// (edit to rename), Move, Fire and (keepers, scientists, marine
// specialists) Assign; Status: "Monthly Salary:" and "Current Duty:";
// Job Assignment: a keeper's exhibits, or a maintenance worker's duties to
// tick. Opened from the Staff List it sits above the list. The list shows
// every staff member (mini portrait, name) and filters with their counts.
// ----------------------------------------------------------------------------
void UiGameScreen::setStaff(Staff *staff, std::function<void(StaffRequest, int)> request) {
  this->staff = staff;
  this->staffRequest = request;
  this->staffListCount = -1;
}

void UiGameScreen::setupStaffPanels() {
  for (Entry &e : this->entries) {
    if (e.panel && e.id == 17)
      this->staffPanel = e.layout;
    if (e.panel && e.id == 31)
      this->staffList = e.layout;
  }
  if (UiLayout *z = this->staffPanel) {
    // The layout hides the name (state=1); the game shows it. Track starts
    // off.
    if (UiElement *name = z->getElementById(3502))
      name->setHidden(false);
    if (UiButton *track = dynamic_cast<UiButton *>(z->getElementById(3515)))
      track->setToggledOn(false);
    // Pick up (zoo.exe infostf_PickupStaff_click 0x618553): Assign off
    // (uiTurnOff 3578), then 0x46ab75 - nothing selected, the panel shut,
    // the member held; put down, the panel opens again (placeObject
    // 0x4d923c, endPlacing 0x443df7: 0x5b0f93)
    if (UiButton *b = dynamic_cast<UiButton *>(z->getElementById(3570)))
      b->onClick = [this, z] {
        const int id = this->shownStaff;
        if (!this->staffRequest || id < 0)
          return;
        if (UiButton *assign = dynamic_cast<UiButton *>(z->getElementById(3578)))
          assign->setToggledOn(false);
        this->setPanelOpen(17, false);
        this->shownStaff = -1;
        this->staffRequest(StaffRequest::PickUp, id);
      };
    if (UiButton *b = dynamic_cast<UiButton *>(z->getElementById(3606)))
      b->onClick = [this] {
        // (asked first, as the original: "Are you sure you want to fire
        // %s?" and the monthly salary - lang 155)
        const Staff::Member *m = this->staff ? this->staff->member(this->shownStaff) : nullptr;
        if (!m)
          return;
        const int id = m->id;
        std::string ask = this->resource_manager->getString(155);
        if (size_t at = ask.find("%s"); at != std::string::npos)
          ask.replace(at, 2, m->name);
        ask += formatPrice(this->staff->types()[m->type].salary);
        this->askConfirm(ask, [this, id] {
          if (this->staffRequest)
            this->staffRequest(StaffRequest::Fire, id);
          if (this->shownStaff == id) {
            this->shownStaff = -1;
            this->setPanelOpen(17, false);
          }
          this->staffListCount = -1;
        });
      };
    // Assign, a toggle (zoo.exe infostf_AssigntoListStaff_click 0x471c30):
    // on, the zoo view picks exhibits for the keeper and the Job Assignment
    // tab (3564) is brought up; off (0x471ebd), it stops picking
    if (UiButton *b = dynamic_cast<UiButton *>(z->getElementById(3578)))
      b->onClick = [this, b, z] {
        if (!this->staffRequest || this->shownStaff < 0)
          return;
        if (!b->isToggledOn()) {
          this->staffRequest(StaffRequest::Assign, -1);
          return;
        }
        this->staffRequest(StaffRequest::Assign, this->shownStaff);
        if (UiButton *tab = dynamic_cast<UiButton *>(z->getElementById(3564))) {
          tab->choose();
          z->syncTabs();
        }
      };
    // (Remove and Remove All turn Assign off first, uiTurnOff 3578: 0x618573,
    // 0x6185d6)
    auto assignOff = [this, z] {
      UiButton *a = dynamic_cast<UiButton *>(z->getElementById(3578));
      if (a && a->isToggledOn()) {
        a->setToggledOn(false);
        if (this->staffRequest)
          this->staffRequest(StaffRequest::Assign, -1);
      }
    };
    if (UiButton *b = dynamic_cast<UiButton *>(z->getElementById(3515)))
      b->onClick = [this] {
        this->tracking = !this->tracking;
        if (this->staffRequest)
          this->staffRequest(StaffRequest::Track, this->tracking ? this->shownStaff : -1);
      };
    if (UiText *name = dynamic_cast<UiText *>(z->getElementById(3502)))
      name->onCommit = [this](const std::string &typed) {
        // (zoo.exe infostf_StaffName_click 0x61847c: setName, 0x449215; a
        // staff member's class slot 0x1c is 0x449515, the rename cheats
        // 0x44940b)
        if (!this->staff || !Original::nameTaken(typed))
          return;
        if (Staff::Member *m = this->staff->member(this->shownStaff))
          m->name = typed;
        this->staffListCount = -1;
        if (this->staffRenamed)
          this->staffRenamed(this->shownStaff, typed);
      };
    // A maintenance worker's duties: ticks (zoo.exe infostf3_MwAssignList_click
    // 0x6186f2: a row clicked flips its bit, staff +0x2a8, and the count of
    // those ticked, +0x2bc)
    if (UiListBox *duties = dynamic_cast<UiListBox *>(z->getElementById(3587))) {
      duties->clear();
      for (int id : {3530, 3531, 3532, 3533})
        duties->addItem(this->resource_manager->getString(id));
    }
    // A keeper's exhibits: clear the picked one, or all
    if (UiButton *b = dynamic_cast<UiButton *>(z->getElementById(3579)))
      b->onClick = [this, assignOff] {
        assignOff();
        UiListBox *l = this->staffPanel ? dynamic_cast<UiListBox *>(this->staffPanel->getElementById(3569)) : nullptr;
        Staff::Member *m = this->staff ? this->staff->member(this->shownStaff) : nullptr;
        if (l && m && l->getSelectedIndex() >= 0 && l->getSelectedIndex() < (int)m->exhibits.size())
          m->exhibits.erase(m->exhibits.begin() + l->getSelectedIndex());
      };
    if (UiButton *b = dynamic_cast<UiButton *>(z->getElementById(3580)))
      b->onClick = [this, assignOff] {
        assignOff();
        if (Staff::Member *m = this->staff ? this->staff->member(this->shownStaff) : nullptr)
          m->exhibits.clear();
      };
    // A keeper's exhibit picked (zoo.exe infostf2_AssignList_click 0x61861b):
    // Assign off (uiTurnOff 3578), Remove enabled (3579's slot 0x68), the
    // view to the exhibit's gate (its habitat +0x8c, the gate's tile, set at
    // 0x435a0d: centred on by 0x59b90c), the Exhibit List shown if it's
    // hidden (0x6108d2: element 30's slot 0x4c) and that exhibit made its
    // pick (0x58b5ce; a show tank: its show panel)
    if (UiListBox *l = dynamic_cast<UiListBox *>(z->getElementById(3569)))
      l->onPick = [this, z, assignOff](int row) {
        assignOff();
        if (UiButton *remove = dynamic_cast<UiButton *>(z->getElementById(3579)))
          remove->setDisabled(false);
        const Staff::Member *m = this->staff ? this->staff->member(this->shownStaff) : nullptr;
        if (!m || row < 0 || row >= (int)m->exhibits.size())
          return;
        const int ex = m->exhibits[row];
        float gx = 0, gy = 0;
        if (this->centreOn && this->exhibitGateTile(ex, gx, gy))
          this->centreOn(gx, gy);
        if (!this->isPanelOpen(30)) {
          // (mulhab_Panel_show 0x46836c: the other lists at the bottom
          // right shut - Animal 16, Guest 12, Staff 31: 0x5b1426 - and Zoo
          // Status and the show panel, closeOtherPanels 0x443af8; the
          // information windows stay, moved above it, 0x4ef99b)
          for (int p : {16, 12, 31, 14, 41, 30}) {
            this->setPanelOpen(p, p == 30);
            for (UiButton *b : this->panelButtons)
              if (b->getActionTarget() == p)
                b->setToggledOn(p == 30);
          }
          this->listedExhibits.clear();
          this->shownListCount = -1;
        }
        if (const Fences::Exhibit *e = this->fences ? this->fences->exhibit(ex) : nullptr; e && e->show) {
          this->showShow(ex);
          return;
        }
        auto centre = this->centreOn;
        this->centreOn = nullptr;
        // (0x58b5ce alone: the exhibit picked, its gate not selected - no
        // ui_main_4b2322 here, unlike the list's own pick)
        this->keeperPicking = true;
        this->refreshExhibits();
        if (UiListBox *list = this->exhibitPanel ? dynamic_cast<UiListBox *>(this->exhibitPanel->getElementById(4305)) : nullptr)
          for (size_t i = 0; i < this->listedExhibits.size(); i++)
            if (this->listedExhibits[i] == ex) {
              list->setSelectedIndex((int)i);
              this->refreshExhibits();
            }
        this->keeperPicking = false;
        this->centreOn = centre;
      };
  }
  // The DRT base's panel (drt.lyt, 1400; its handlers registered by
  // register_drt_handlers 0x516f07)
  for (Entry &e : this->entries)
    if (e.panel && e.id == 1400)
      this->drtPanel = e.layout;
  if (UiLayout *d = this->drtPanel) {
    if (UiElement *name = d->getElementById(1403))
      name->setHidden(false);
    // Its name (drt_DRTName_click 0x61615a: setName, as the others -
    // 0x449215, Original::nameTaken)
    if (UiText *name = dynamic_cast<UiText *>(d->getElementById(1403)))
      name->onCommit = [this](const std::string &typed) {
        if (!this->staff || !Original::nameTaken(typed))
          return;
        if (Staff::Member *m = this->staff->member(this->shownDrt))
          m->name = typed;
        this->staffListCount = -1;
        this->drtRefreshNow = true;
      };
    // TrackDRT (infoTrack_click 0x48c7ef / infoTrack_off 0x616231: the
    // view follows its helicopter, view +0x43c, as Track Staff)
    if (UiButton *b = dynamic_cast<UiButton *>(d->getElementById(1405)))
      b->onClick = [this] {
        this->tracking = !this->tracking;
        if (this->staffRequest)
          this->staffRequest(StaffRequest::Track, this->tracking ? this->shownDrt : -1);
      };
    // Fire Team (infoSellOrFire_click 0x48d0b4 -> removeObject 0x50a0b0:
    // a base of type 8031 asks as firing its helicopter does - text 155,
    // "Are you sure you want to fire %s?", and the helicopter's monthly
    // salary, its slot 0x314)
    if (UiButton *b = dynamic_cast<UiButton *>(d->getElementById(1419)))
      b->onClick = [this] {
        const Staff::Member *m = this->staff ? this->staff->member(this->shownDrt) : nullptr;
        if (!m)
          return;
        const int id = m->id;
        std::string ask = this->resource_manager->getString(155);
        if (size_t at = ask.find("%s"); at != std::string::npos)
          ask.replace(at, 2, m->name);
        ask += formatPrice(this->staff->types()[m->type].salary);
        this->askConfirm(ask, [this, id] {
          if (this->staffRequest)
            this->staffRequest(StaffRequest::Fire, id);
          if (this->shownDrt == id) {
            this->shownDrt = -1;
            this->setPanelOpen(1400, false);
          }
          this->staffListCount = -1;
        });
      };
  }
  if (UiLayout *l = this->staffList) {
    // The filters: all, keepers, scientists, marine specialists,
    // maintenance workers, DRT, guides, assigned, unassigned
    const int buttons[9] = {4215, 4217, 4240, 4244, 4221, 4242, 4219, 4223, 4225};
    // (hidden in the layout, state=1; the game shows them)
    for (int id : buttons)
      if (UiElement *e = l->getElementById(id))
        e->setHidden(false);
    if (UiElement *e = l->getElementById(4235))
      e->setHidden(false);
    for (int i = 0; i < 9; i++)
      if (UiButton *b = dynamic_cast<UiButton *>(l->getElementById(buttons[i])))
        b->onClick = [this, i] {
          this->staffFilter = i;
          this->staffListCount = -1;
        };
    // No keys on the list: zoo.exe registers a Delete handler for it
    // (mulstaff_list_pickB 0x61841d: key 0x2e -> sellSelected 0x48d052) on
    // the UI's event table 0x6391ec, which nothing fires - every uiDispatch
    // 0x41ab24 names another table - and Delete on a picked staff member
    // does nothing in the running game (measured, #270)
  }
}

void UiGameScreen::showStaff(int id) {
  if (!this->staffPanel || !this->staff)
    return;
  const Staff::Member *m = this->staff->member(id);
  if (!m)
    return;
  this->openInfo(17);
  this->shownStaff = id;
  this->staff->selected = id;
  // (zoo.exe infostf_Panel_show 0x4716b2: TrackStaff turned off, slot 0x54,
  // and drawn on it; its name; then 0x47108a's fill)
  this->tracking = false;
  if (UiButton *track = dynamic_cast<UiButton *>(this->staffPanel->getElementById(3515)))
    track->setToggledOn(false);
  this->pickStaffRow(id);
  const Staff::Type &t = this->staff->types()[m->type];
  // (tour guides can be given exhibits too: their tours go to those)
  bool keeperLike = t.kind == Staff::Kind::Keeper || t.kind == Staff::Kind::Scientist ||
                    t.kind == Staff::Kind::Trainer || t.kind == Staff::Kind::Guide;
  bool maint = t.kind == Staff::Kind::Maint;
  UiLayout *z = this->staffPanel;
  auto show = [&](int eid, bool on) {
    if (UiElement *e = z->getElementById(eid))
      e->setHidden(!on);
  };
  show(3578, keeperLike);
  show(3564, keeperLike);
  show(3565, maint);
  // Every one opens on Status (seen side by side in the Lab's Play: the
  // original's keeper with no exhibits, picked from the Staff List or on
  // the map, shows its salary, duty and training)
  UiButton *tab = dynamic_cast<UiButton *>(z->getElementById(3562));
  if (tab)
    tab->choose();
  z->syncTabs();
  if (UiListBox *duties = dynamic_cast<UiListBox *>(z->getElementById(3587)))
    for (int i = 0; i < 4; i++)
      duties->setChecked(i, m->duties[i]);
  this->refreshStaff();
}

// The tile by an exhibit's gate, its middle. zoo.exe: the habitat's +0x8c,
// the tile its gate piece is reached from (the gate itself through
// 0x492ca3, the tile's +0x14 by +0x90's side) - measured: in the running
// original (2026-11-17, a paddock at 39..46 x 32..39 with its gate on the top
// side at x 43) +0x8c was tile 43,31, the gate's outside tile, +0x90 4
bool UiGameScreen::exhibitGateTile(int id, float &x, float &y) const {
  if (!this->fences)
    return false;
  for (const auto &[e, p] : this->fences->pieces())
    if (p.gate && this->fences->exhibitOf(e) == id) {
      int ax = e.alongX ? e.x : e.x - 1, ay = e.alongX ? e.y - 1 : e.y;
      if (this->fences->exhibitAt(ax, ay) == id) {
        ax = e.x;
        ay = e.y;
      }
      x = ax + 0.5f;
      y = ay + 0.5f;
      return true;
    }
  return false;
}

// A DRT base clicked: its panel (zoo.exe ui_main_4b2322 -> 0x5b66b1: a
// building of type 8031, the helibase, ui_drt_48cd7a - drt.lyt shown;
// drt_Panel_show 0x48ccc1: TrackDRT off, its name, then 0x48c87d's fill).
// The Staff List's DRT picks it too (mulstaff_list_click 0x4720cc: the
// helicopter's base, 0x5066bb). ours: the base and its helicopter are one
// staff member; picking either on the map opens this
void UiGameScreen::showDrt(int id) {
  if (!this->drtPanel || !this->staff || !this->staff->member(id))
    return;
  this->openInfo(1400);
  this->shownDrt = id;
  this->staff->selected = id;
  this->tracking = false;
  // (the layout hides TrackDRT, state=2048, as the animal panel's
  // Zookeeper Recommendations; the game shows it, the helicopter drawn in
  // it - measured, #266)
  if (UiButton *track = dynamic_cast<UiButton *>(this->drtPanel->getElementById(1405))) {
    track->setHidden(false);
    track->setToggledOn(false);
  }
  this->drtRefreshNow = true;
  this->pickStaffRow(id);
  this->refreshDrt();
}

// The Staff List's pick follows the selection (zoo.exe 0x5b66b1 ->
// ui_mulstaff_47158c 0x47158c with the unit: its row in the list's order,
// listSelectIndex 0x4189e8; none, or not listed, -1)
void UiGameScreen::pickStaffRow(int id) {
  UiListBox *list = this->staffList ? dynamic_cast<UiListBox *>(this->staffList->getElementById(4205)) : nullptr;
  if (!list)
    return;
  int row = -1;
  for (size_t i = 0; i < this->listedStaff.size() && i < list->getItemCount(); i++)
    if (this->listedStaff[i] == id)
      row = (int)i;
  list->setSelectedIndex(row);
}

// zoo.exe 0x48c87d (on show, and every 1000 ms while it's shown, 0x41a3f3):
// 1407 the helicopter's monthly salary (slot 0x314, setMoneyText 0x40ed88),
// 1411 its duty (slot 0x318), 1409 the base's recoveries (+0x19c, a whole
// number, 0x417879), TrackDRT enabled again (0x443e3e); 1413-1417, a
// trained keeper's icons, aren't in drt.lyt
void UiGameScreen::refreshDrt() {
  UiLayout *d = this->drtPanel;
  if (!d || !this->staff)
    return;
  const Staff::Member *m = this->staff->member(this->shownDrt);
  if (this->isPanelOpen(1400) && !m)
    this->setPanelOpen(1400, false);
  if (!this->isPanelOpen(1400)) {
    if (this->shownDrt >= 0) {
      if (this->staff->selected == this->shownDrt)
        this->staff->selected = -1;
      if (this->tracking && this->staffRequest)
        this->staffRequest(StaffRequest::Track, -1);
      this->tracking = false;
      this->shownDrt = -1;
    }
    return;
  }
  const uint32_t now = static_cast<uint32_t>(SDL_GetTicks());
  if (!this->drtRefreshNow && now < this->drtRefreshAt)
    return;
  this->drtRefreshNow = false;
  this->drtRefreshAt = now + 1000;
  auto text = [&](int eid, const std::string &s) {
    if (UiText *e = dynamic_cast<UiText *>(d->getElementById(eid)))
      if (!e->isBeingEdited())
        e->setText(s);
  };
  text(1403, m->name);
  text(1407, formatCost(this->staff->types()[m->type].salary));
  text(1411, this->staff->dutyText(*m));
  text(1409, std::to_string(m->recoveries));
  if (UiElement *track = d->getElementById(1405))
    track->setDisabled(false);
}

void UiGameScreen::refreshStaff() {
  if (!this->staff)
    return;
  this->refreshDrt();
  // Staff Information
  if (UiLayout *z = this->staffPanel) {
    Staff::Member *m = this->staff->member(this->shownStaff);
    if (this->isPanelOpen(17) && !m)
      this->setPanelOpen(17, false);
    // (closed - its CloseWindow, infoPanel_CloseWindow_click 0x599c18 ->
    // 0x4ef75b: ui_main_4b2322(0, 1), nothing selected - tracking ends)
    if (!this->isPanelOpen(17) && this->shownStaff >= 0) {
      if (this->staff->selected == this->shownStaff)
        this->staff->selected = -1;
      this->shownStaff = -1;
      if (this->tracking && this->staffRequest)
        this->staffRequest(StaffRequest::Track, -1);
      this->tracking = false;
    }
    if (m && this->isPanelOpen(17)) {
      const Staff::Type &t = this->staff->types()[m->type];
      auto text = [&](int eid, const std::string &s) {
        if (UiText *e = dynamic_cast<UiText *>(z->getElementById(eid)))
          if (!e->isBeingEdited())
            e->setText(s);
      };
      text(3502, m->name);
      if (UiButton *a = dynamic_cast<UiButton *>(z->getElementById(3578))) {
        if (a->isToggledOn() && this->staffAssigning && !this->staffAssigning())
          a->setToggledOn(false);
        // (the Job Assignment page hidden - another tab picked: Assign off,
        // infostf2_Panel_hide 0x4715ed, uiTurnOff 3578)
        UiButton *job = dynamic_cast<UiButton *>(z->getElementById(3564));
        if (a->isToggledOn() && job && !job->isToggledOn()) {
          a->setToggledOn(false);
          if (this->staffRequest)
            this->staffRequest(StaffRequest::Assign, -1);
        }
      }
      text(3503, formatCost(t.salary));
      text(3505, this->staff->dutyText(*m));
      if (UiListBox *duties = dynamic_cast<UiListBox *>(z->getElementById(3587)))
        for (int i = 0; i < 4; i++)
          m->duties[i] = duties->isChecked(i);
      // (zoo.exe 0x47108a: the list refilled, its pick kept, 0x4ef81c;
      // Remove enabled only while a row is picked - slot 0x68 / 0x6c)
      if (UiListBox *l = dynamic_cast<UiListBox *>(z->getElementById(3569))) {
        if ((int)l->getItemCount() != (int)m->exhibits.size()) {
          const int pick = l->getSelectedIndex();
          l->clear();
          for (int ex : m->exhibits)
            if (const Fences::Exhibit *e = this->fences ? this->fences->exhibit(ex) : nullptr)
              l->addItem(e->name);
          l->setSelectedIndex(pick < (int)l->getItemCount() ? pick : -1);
        }
        if (UiButton *remove = dynamic_cast<UiButton *>(z->getElementById(3579)))
          remove->setDisabled(l->getSelectedIndex() < 0);
      }
    }
  }
  // Staff List
  if (UiLayout *l = this->staffList) {
    if (!this->isPanelOpen(31))
      return;
    const auto &all = this->staff->members();
    auto kindOf = [&](const Staff::Member &m) { return this->staff->types()[m.type].kind; };
    auto assigned = [&](const Staff::Member &m) { return !m.exhibits.empty(); };
    // (Assigned and Unassigned count every staff member but maintenance
    // workers and the DRT - tour guides too: zoo.exe 0x457d70, staff
    // 0x638710 and not maint 0x638780 nor helicopter 0x638790)
    auto keeperLike = [&](const Staff::Member &m) {
      Staff::Kind k = kindOf(m);
      return k == Staff::Kind::Keeper || k == Staff::Kind::Scientist || k == Staff::Kind::Trainer ||
             k == Staff::Kind::Guide;
    };
    // The filters as the original's buttons pass them to the list refresh
    // (zoo.exe 0x469f0b, matched by 0x457d70): all (mulstaff_FilterAll_click
    // 0x469fbb), keepers (0x469798), scientists (0x1090_click 0x4697a1),
    // marine specialists (0x1094_click 0x469753), maintenance (FilterMman
    // 0x46975c), DRT (0x1092_click 0x46978f), guides (FilterTguide
    // 0x469786), assigned (0x46977d), unassigned (0x469765) - here in the
    // panel's button order
    auto passes = [&](const Staff::Member &m, int f) {
      switch (f) {
      case 1: return kindOf(m) == Staff::Kind::Keeper;
      case 2: return kindOf(m) == Staff::Kind::Scientist;
      case 3: return kindOf(m) == Staff::Kind::Trainer;
      case 4: return kindOf(m) == Staff::Kind::Maint;
      case 5: return kindOf(m) == Staff::Kind::Helicopter;
      case 6: return kindOf(m) == Staff::Kind::Guide;
      case 7: return keeperLike(m) && assigned(m);
      case 8: return keeperLike(m) && !assigned(m);
      default: return true;
      }
    };
    const int counts[9] = {4216, 4218, 4241, 4245, 4222, 4243, 4220, 4224, 4226};
    for (int f = 0; f < 9; f++) {
      int n = 0;
      for (const Staff::Member &m : all)
        n += passes(m, f) ? 1 : 0;
      if (UiText *t = dynamic_cast<UiText *>(l->getElementById(counts[f])))
        t->setText(std::to_string(n));
    }
    static const int names[9] = {4232, 4233, 4241, 4244, 4235, 4243, 4234, 4236, 4237};
    if (UiText *t = dynamic_cast<UiText *>(l->getElementById(4235)))
      t->setText(this->resource_manager->getString(names[this->staffFilter]));
    UiListBox *list = dynamic_cast<UiListBox *>(l->getElementById(4205));
    if (!list)
      return;
    std::vector<int> shown;
    for (const Staff::Member &m : all)
      if (passes(m, this->staffFilter))
        shown.push_back(m.id);
    // (zoo.exe keeps its rows as staff come and go: one added at the end as
    // a staff member's put in the world, its name +0x108 and its type's list
    // icon - ui_mulstaff_457f81 0x457f81, from 0x449ed0 - and taken out as
    // it's removed, ui_mulstaff_501f2b 0x501f2b from 0x4586f6; ours: the
    // rows made again, in the same hiring order, whenever they differ)
    bool relist = (int)shown.size() != this->staffListCount || shown != this->listedStaff;
    if (relist) {
      list->clear();
      for (int id : shown) {
        const Staff::Member *m = this->staff->member(id);
        const Staff::Type &t = this->staff->types()[m->type];
        // (its cListImageName as art, as the Animal List's: the raw frame
        // reader drew the zookeeper's 17 x 32 at half its size)
        const std::string &path = t.listImage[m->female ? 1 : 0];
        if (!path.empty() && this->resource_manager->hasResource(path + ".ani"))
          list->addItem(m->name, this->resource_manager->getAnimation(path));
        else
          list->addItem(m->name, "", path);
      }
      this->listedStaff = shown;
      this->staffListCount = (int)shown.size();
      list->setSelectedIndex(-1);
      for (size_t i = 0; i < shown.size(); i++)
        if (shown[i] == this->shownStaff || shown[i] == this->shownDrt)
          list->setSelectedIndex((int)i);
    }
    // (zoo.exe mulstaff_list_click 0x4720cc: the unit selected, ui_main_4b2322
    // - a helicopter (0x638790) as its base, +0x294 through 0x5066bb, so the
    // DRT base's panel - and the view to its tile, 0x59b90c, unless tracking)
    int sel = list->getSelectedIndex();
    if (sel >= 0 && sel < (int)this->listedStaff.size() && this->listedStaff[sel] != this->shownStaff &&
        this->listedStaff[sel] != this->shownDrt) {
      int id = this->listedStaff[sel];
      if (this->staffRequest)
        this->staffRequest(StaffRequest::Select, id);
      const Staff::Member *pick = this->staff->member(id);
      if (pick && this->staff->types()[pick->type].kind == Staff::Kind::Helicopter)
        this->showDrt(id);
      else
        this->showStaff(id);
    }
  }
}

// ----------------------------------------------------------------------------
// Animal List and Guest List (measured against the original's: "All
// Animals", each one's mini icon and name, the filters' counts: all,
// angry, sick, hungry, unhappy with its exhibit, escaped; "All Guests":
// all, angry, thirsty, hungry, restroom, tired). Picking one opens its
// information (above the list) and centres the view on it.
// ----------------------------------------------------------------------------
static const int kAnimalFilters[6] = {3815, 3817, 3819, 3821, 3823, 3838};
static const int kAnimalCounts[6] = {3816, 3818, 3820, 3822, 3824, 3839};
static const int kAnimalNames[6] = {3825, 3826, 3827, 3828, 3829, 3840};
static const int kGuestFilters[6] = {3715, 3717, 3719, 3721, 3723, 3725};
static const int kGuestCounts[6] = {3716, 3718, 3720, 3722, 3724, 3726};
static const int kGuestNames[6] = {3727, 3728, 3729, 3730, 3731, 3732};

void UiGameScreen::setupLists() {
  for (Entry &e : this->entries) {
    if (e.panel && e.id == 16)
      this->animalListPanel = e.layout;
    if (e.panel && e.id == 12)
      this->guestListPanel = e.layout;
  }
  auto hook = [](UiLayout *z, const int *buttons, int *chosen) {
    if (!z)
      return;
    for (int i = 0; i < 6; i++)
      if (UiButton *b = dynamic_cast<UiButton *>(z->getElementById(buttons[i]))) {
        b->setHidden(false);
        b->onClick = [chosen, i] { *chosen = i; };
      }
  };
  // The Messages button (a toggle with no target) shows the Message List
  // (zoo.exe main_Messages_click 0x4a0399: uiFind(29), shown or hidden;
  // let go, main_Messages_off 0x4a0463 hides it)
  // (its list and scrollbar hidden in the layout, state=1)
  for (Entry &e : this->entries)
    if (e.panel && e.id == 29)
      for (int id : {7201, 7202})
        if (UiElement *el = e.layout->getElementById(id))
          el->setHidden(false);
  if (UiButton *b = this->hud ? dynamic_cast<UiButton *>(this->hud->getElementById(1006)) : nullptr) {
    b->setToggledOn(false);
    b->onClick = [this, b] { this->setPanelOpen(29, b->isToggledOn()); };
  }
  // (the filter's name: hidden in the layouts, state=1; the game shows it)
  for (UiLayout *z : {this->animalListPanel, this->guestListPanel})
    if (z)
      for (int id : {3835, 3727})
        if (UiElement *e = z->getElementById(id))
          e->setHidden(false);
  hook(this->animalListPanel, kAnimalFilters, &this->animalFilter);
  hook(this->guestListPanel, kGuestFilters, &this->guestFilter);
  // (no keys on the Animal List either: its Delete handler,
  // mulanim_list_pickB 0x618451, sits on the event table 0x6391ec that
  // nothing fires - Delete on a picked animal does nothing in the running
  // game, measured, #270)
}

// The filters (zoo.exe 0x4537fe, 0x4f5d7f), T = cUIThreshold (75 in both
// animals.ai and guests.ai): angry at happiness 2T - 201 or less (-51 on
// the -100..100 scale), hungry and the other needs over T; unhappy below
// its species' cHabitatPreference (outside an exhibit: 0, so unhappy)
static constexpr int kUiThreshold = 75;

// The Animal List's filters by number, as the original's buttons pass them to
// animalListRefresh (zoo.exe 0x46956e): All 0 (0x46961e), Angry 1 (0x4697fa),
// Sick 2 (0x4697f1), Hungry 3 (0x46972c), unhappy with its exhibit 4
// (0x618448), Escaped 5 (0x4697e8)
bool UiGameScreen::animalPasses(const Animals::Member &m, int filter) const {
  const Animals::Type &t = this->animals->types()[m.type];
  switch (filter) {
  case 1: return m.happiness <= 2 * kUiThreshold - 201;
  case 2: return m.sick;
  case 3: return m.hunger > kUiThreshold;
  case 4: {
    if (m.boxed || m.dying || m.id == this->animals->carried)
      return false;
    int s = m.exhibit >= 0 && !m.escaped && this->animalSuitability ? this->animalSuitability(m.id) : 0;
    return s < t.habitatPreference;
  }
  case 5: return m.escaped;
  default: return true;
  }
}

// The Guest List's, as its buttons pass them to the list refresh (zoo.exe
// 0x469c0f, guestFilterMatches 0x4f5d7f): All 0 (0x469cbf), Angry 1
// (0x4696dd), Thirsty 2 (0x469803), Hungry 3 (0x4696c5), Restroom 4
// (0x46980c), Tired 5 (0x469815)
bool UiGameScreen::guestPasses(const Guests::Guest &g, int filter) const {
  switch (filter) {
  case 1: return g.happiness <= 2 * kUiThreshold - 201;
  case 2: return g.thirst > kUiThreshold;
  case 3: return g.hunger > kUiThreshold;
  case 4: return g.bathroom > kUiThreshold;
  case 5: return g.tired > kUiThreshold;
  default: return true;
  }
}

void UiGameScreen::refreshLists() {
  auto text = [](UiLayout *z, int id, const std::string &s) {
    if (UiText *t = dynamic_cast<UiText *>(z->getElementById(id)))
      t->setText(s);
  };
  // The list itself: rows as the filter has them, the one selected in the
  // zoo lit; a pick shows it
  auto fill = [](UiListBox *list, const std::vector<int> &ids, std::vector<int> &listed, int &lit, int selected,
                 const std::function<std::pair<std::string, std::string>(int)> &row,
                 const std::function<Animation *(int)> &art = nullptr) -> int {
    int want = -1;
    for (size_t i = 0; i < ids.size(); i++)
      if (ids[i] == selected)
        want = static_cast<int>(i);
    if (ids != listed) {
      int top = list->getScrollPosition();
      list->clear();
      for (int id : ids) {
        auto [name, icon] = row(id);
        if (Animation *a = art ? art(id) : nullptr)
          list->addItem(name, a);
        else
          list->addItem(name, "", icon);
      }
      listed = ids;
      lit = want;
      list->setSelectedIndex(want);
      list->setScrollPosition(top);
      return -1;
    }
    int pick = list->getSelectedIndex();
    if (pick != lit && pick >= 0 && pick < static_cast<int>(ids.size())) {
      lit = pick;
      return ids[pick];
    }
    if (want != lit) {
      lit = want;
      list->setSelectedIndex(want);
    }
    return -1;
  };
  // The Message List: every message kept, newest at the top, each with its
  // arrow; the Messages button lit while it's open (closed: let go -
  // zoo.exe message_Panel_hide 0x4a03fb: uiFind 0x3ee, slot 0x54)
  for (Entry &e : this->entries)
    if (e.panel && e.id == 29) {
      if (UiButton *b = this->hud ? dynamic_cast<UiButton *>(this->hud->getElementById(1006)) : nullptr)
        b->setToggledOn(e.open);
      if (!e.open)
        break;
      if (UiListBox *list = dynamic_cast<UiListBox *>(e.layout->getElementById(7201)))
      {
        // (measured: "The game is paused." first while it is, white and
        // without a mark, then the news, newest first, in its colour; news
        // about something has the crosshair: ui/sharedui/mescros)
        std::string current = this->pausedMessage ? this->resource_manager->getString(400) : std::string();
        int offset = current.empty() ? 0 : 1;
        if (this->messagesListedSerial != this->messageSerial || current != this->messageListTop) {
          list->clear();
          if (!current.empty())
            list->addItem(current, SDL_Color{255, 255, 255, 255});
          static const SDL_Color colours[3] = {{255, 255, 255, 255}, {0, 255, 0, 255}, {255, 0, 0, 255}};
          for (const LoggedMessage &m : this->messageLog) {
            list->addItem(m.text, "", m.subject != Subject::None ? "ui/sharedui/mescros/mescros" : "");
            list->setItemColor(static_cast<int>(list->getItemCount()) - 1, colours[m.kind % 3]);
          }
          this->messagesListedSerial = this->messageSerial;
          this->messageListTop = current;
          list->setSelectedIndex(-1);
        }
        // A click on news about something: the view goes there and it's
        // picked (zoo.exe message_MessageList_click 0x618d5c -> 0x47fa78)
        int pick = list->getSelectedIndex() - offset;
        if (pick >= 0 && pick < static_cast<int>(this->messageLog.size())) {
          const LoggedMessage &m = this->messageLog[pick];
          list->setSelectedIndex(-1);
          if (m.subject != Subject::None)
            this->bulldozerOff();
          if (m.subject == Subject::Animal && this->animals && this->animals->member(m.id)) {
            this->showAnimal(m.id);
            if (this->animalRequest)
              this->animalRequest(AnimalRequest::Select, m.id);
          } else if (m.subject == Subject::Guest && this->guests) {
            if (const Guests::Guest *g = this->guests->guest(m.id)) {
              this->showGuest(m.id);
              if (this->centreOn)
                this->centreOn(g->x, g->y);
            }
          } else if (m.subject == Subject::Staff && this->staff && this->staff->member(m.id)) {
            if (this->staffRequest)
              this->staffRequest(StaffRequest::Select, m.id);
            this->showStaff(m.id);
          }
        } else if (pick < 0 && list->getSelectedIndex() >= 0) {
          list->setSelectedIndex(-1);
        }
      }
    }
  // (the chosen filter's count in white, the rest in their own colour)
  auto countColour = [](UiLayout *z, int id, bool chosen) {
    if (UiText *t = dynamic_cast<UiText *>(z->getElementById(id))) {
      if (chosen)
        t->setTextColor(SDL_Color{255, 255, 255, 255});
      else
        t->clearTextColor();
    }
  };
  if (UiLayout *z = this->animalListPanel; z && this->animals && this->isPanelOpen(16)) {
    // (zoo.exe animalListFill 0x45390c: the list 0xedd - 3805 - cleared,
    // each animal in the zoo's list passing the filter (animalFilterMatches
    // 0x4537fe) added, the selected one (selectedObject 0x410f84) picked,
    // none picked otherwise; the six filters' counts (0x63e2f0) into their
    // texts)
    std::vector<int> ids;
    for (int f = 0; f < 6; f++) {
      int n = 0;
      for (const Animals::Member &m : this->animals->members())
        if (this->animalPasses(m, f)) {
          n++;
          if (f == this->animalFilter)
            ids.push_back(m.id);
        }
      text(z, kAnimalCounts[f], std::to_string(n));
      countColour(z, kAnimalCounts[f], f == this->animalFilter);
    }
    // (by species, then name, then when it came: zoo.exe 0x453643)
    std::stable_sort(ids.begin(), ids.end(), [this](int a, int b) {
      const Animals::Member *x = this->animals->member(a), *y = this->animals->member(b);
      const auto &tx = this->animals->types()[x->type], &ty = this->animals->types()[y->type];
      std::string sx = !tx.displayName.empty() ? tx.displayName : this->resource_manager->getString(tx.nameId);
      std::string sy = !ty.displayName.empty() ? ty.displayName : this->resource_manager->getString(ty.nameId);
      if (sx != sy)
        return sx < sy;
      if (x->name != y->name)
        return x->name < y->name;
      return a < b;
    });
    text(z, 3835, this->resource_manager->getString(kAnimalNames[this->animalFilter]));
    if (UiListBox *list = dynamic_cast<UiListBox *>(z->getElementById(3805))) {
      int picked = fill(
          list, ids, this->listedAnimals, this->litAnimal, this->animals->selected,
          [this](int id) {
            const Animals::Member *m = this->animals->member(id);
            return std::make_pair(m->name, this->animals->types()[m->type].listImage);
          },
          // (its cListImageName as art: Marine Mania's - the porpoise's
          // lsporpoi, 37 x 14 - came out a few pixels wide from the raw
          // frame reader; centred in its row, as the .ani's bounds round
          // the middle have it)
          [this](int id) -> Animation * {
            const Animals::Member *m = this->animals->member(id);
            const std::string &path = this->animals->types()[m->type].listImage;
            return path.empty() || !this->resource_manager->hasResource(path + ".ani")
                       ? nullptr
                       : this->resource_manager->getAnimation(path);
          });
      // (zoo.exe mulanim_list_click 0x5a599c: the row's animal, if it's
      // still in the zoo, the bulldozer put away, the animal selected -
      // ui_main_4b2322, so its panel - and the view centred on its tile,
      // centreViewOn 0x59b90c)
      if (picked >= 0) {
        this->bulldozerOff();
        this->showAnimal(picked);
        if (this->animalRequest)
          this->animalRequest(AnimalRequest::Select, picked);
      }
    }
  }
  if (UiLayout *z = this->guestListPanel; z && this->guests && this->isPanelOpen(12)) {
    std::vector<int> ids;
    for (int f = 0; f < 6; f++) {
      int n = 0;
      for (const Guests::Guest &g : this->guests->guests())
        if (g.x > -999 && this->guestPasses(g, f)) {
          n++;
          if (f == this->guestFilter)
            ids.push_back(g.id);
        }
      text(z, kGuestCounts[f], std::to_string(n));
      countColour(z, kGuestCounts[f], f == this->guestFilter);
    }
    text(z, 3727, this->resource_manager->getString(kGuestNames[this->guestFilter]));
    // (the list 3705: a guest's row added as it arrives if it passes the
    // filter, its name and its kind's list icon, zoo.exe 0x4f5eb2; the row
    // of the guest selected lit, none if it isn't listed, 0x5b0199; a click
    // - mulguest_list_click 0x46ab65 -> guestListGoTo 0x46aa89 - the
    // bulldozer off, the guest selected and shown, the view taken to it)
    if (UiListBox *list = dynamic_cast<UiListBox *>(z->getElementById(3705))) {
      int picked = fill(list, ids, this->listedGuests, this->litGuest, this->guests->selected, [this](int id) {
        const Guests::Guest *g = this->guests->guest(id);
        static const char *icons[4] = {"guests/lsmguest/lsmguest", "guests/lsfguest/lsfguest",
                                       "guests/lsbguest/lsbguest", "guests/lsgguest/lsgguest"};
        int kind = static_cast<int>(this->guests->types()[g->type].kind);
        return std::make_pair(g->name, std::string(icons[std::clamp(kind, 0, 3)]));
      }, [this](int id) { return this->guests->listIcon(*this->guests->guest(id)); });
      if (picked >= 0)
        if (const Guests::Guest *g = this->guests->guest(picked)) {
          this->bulldozerOff();
          this->showGuest(picked);
          if (this->centreOn)
            this->centreOn(g->x, g->y);
        }
    }
  }
}

// The round portrait at the panel's top left: the staff member itself, in
// its colours
void UiGameScreen::drawStaffPortrait(SDL_Renderer *renderer) {
  // The DRT base's panel: its helicopter in the round TrackDRT button
  // (zoo.exe drt_Panel_show 0x48ccc1 -> 0x4ee2ba with the base's helicopter,
  // +0x158's first, and -30005: its animation's frame centred in the
  // button; measured: the original shows the helicopter idle on its pad with
  // its crew, kept to the round window), as the animal panel's portrait
  if (this->staff && this->drtPanel && this->isPanelOpen(1400))
    if (const Staff::Member *m = this->staff->member(this->shownDrt))
      if (UiElement *track = this->drtPanel->getElementById(1405)) {
        Animation *a = this->staff->art(*m, m->anim.empty() ? "idle" : m->anim);
        const SDL_Rect clip = track->getLastRect();
        float l, t, r, b;
        if (a && clip.w > 0 && clip.h > 0 && a->anchoredBounds(0, 0, CompassDirection::SE, l, t, r, b)) {
          const float cx = clip.x + clip.w * 0.5f, cy = clip.y + clip.h * 0.5f;
          const int frame = this->staff->helicopterFrame(*m, a);
          drawRound(renderer, clip, [&](float ox, float oy) {
            a->drawAnchored(renderer, cx - (l + r) * 0.5f + ox, cy - (t + b) * 0.5f + oy, CompassDirection::SE,
                            nullptr, frame);
          });
        }
      }
  if (!this->staff || !this->isPanelOpen(17))
    return;
  const Staff::Member *m = this->staff->member(this->shownStaff);
  if (!m)
    return;
  for (const Entry &e : this->entries)
    if (e.panel && e.id == 17 && e.open)
      if (Animation *a = this->staff->art(*m, "idle"))
        a->drawAnchored(renderer, static_cast<float>(e.rect.x + 34),
                        static_cast<float>(e.rect.y + 62), CompassDirection::SE, nullptr, 0);
}

// ----------------------------------------------------------------------------
// Animal Information (measured in the original): its name (edit to
// rename), Move, Exhibit Information, Sell (asks first: "Are you sure you
// want to sell Plains Zebra 1? Refund: $528"), Zookeeper Recommendations;
// Status: happiness, hunger, health and exhibit suitability bars; General:
// gender, when it last ate and slept; Thoughts: what guests think of it.
// ----------------------------------------------------------------------------
void UiGameScreen::setAnimals(Animals *animals, std::function<void(AnimalRequest, int)> request) {
  this->animals = animals;
  this->animalRequest = request;
}

// The Help window (zoo.exe help_Panel_show 0x484a6b, help_TopicList_click
// 0x48482b, help_Panel_hide 0x484bc1, openHelp 0x484a32): the topics of
// help.cfg's [Topics], each section's title (a string number) in the list and
// its text (a lang0.dll TEXT name, HELP_...) in the window when it's picked,
// the title above it; the first topic, About, has its Version and product
// number put in (the text's two %s). Opened by F1 (zoo.exe gameKeys
// 0x4cb340: openHelp(-1), the topic it was left on, the first at the start)
// and Game Options' About tab (gameopts_AboutTab_click 0x484b9a: topic 0 -
// ours: that tab's place holds the Extras page, so F1 only); the game waits
// while it's open (0x484a8e: the paused flag set, put back on closing)
void UiGameScreen::setupHelp() {
  for (Entry &e : this->entries)
    if (e.panel && e.id == 38)
      this->helpPanel = e.layout;
  UiLayout *z = this->helpPanel;
  if (!z)
    return;
  IniReader *cfg = this->resource_manager->getIniReader("help.cfg");
  if (!cfg)
    return;
  auto trim = [](std::string s) {
    const char *space = " \t\r\n";
    s.erase(0, s.find_first_not_of(space));
    s.erase(s.find_last_not_of(space) + 1);
    return s;
  };
  UiListBox *list = dynamic_cast<UiListBox *>(z->getElementById(28003));
  for (const std::string &name : cfg->getList("topics", "topic")) {
    const int title = cfg->getInt(name, "title", 0);
    if (!title)
      continue;
    HelpTopic t;
    t.title = this->resource_manager->getString(title);
    t.text = trim(cfg->get(name, "text"));
    this->helpTopics.push_back(t);
    if (list)
      list->addItem(t.title);
  }
  delete cfg;
}

void UiGameScreen::openHelp(int topic) {
  if (!this->helpPanel || this->helpTopics.empty())
    return;
  this->setPanelOpen(38, true);
  this->helpShown = -1;
  this->showHelpTopic(topic >= 0 ? topic : this->helpLast);
}

// (the help texts are Windows-1252; the font takes UTF-8)
static std::string windows1252ToUtf8(const std::string &in) {
  static const unsigned short high[32] = {0x20AC, 0x81,   0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021,
                                          0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0x8D,   0x017D, 0x8F,
                                          0x90,   0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014,
                                          0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0x9D,   0x017E, 0x0178};
  std::string out;
  for (unsigned char c : in) {
    unsigned cp = c < 0x80 || c >= 0xA0 ? c : high[c - 0x80];
    if (cp < 0x80) {
      out += static_cast<char>(cp);
    } else if (cp < 0x800) {
      out += static_cast<char>(0xC0 | (cp >> 6));
      out += static_cast<char>(0x80 | (cp & 0x3F));
    } else {
      out += static_cast<char>(0xE0 | (cp >> 12));
      out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
      out += static_cast<char>(0x80 | (cp & 0x3F));
    }
  }
  return out;
}

void UiGameScreen::showHelpTopic(int row) {
  UiLayout *z = this->helpPanel;
  if (!z || row < 0 || row >= (int)this->helpTopics.size())
    return;
  this->helpShown = this->helpLast = row;
  const HelpTopic &t = this->helpTopics[row];
  std::string text = langText(t.text);
  if (row == 0) {
    // (About: sprintf(text, Version, product number))
    for (const std::string &fill : {this->helpVersion, this->helpProductId})
      if (size_t at = text.find("%s"); at != std::string::npos)
        text.replace(at, 2, fill);
  }
  if (UiListBox *list = dynamic_cast<UiListBox *>(z->getElementById(28003)))
    if (list->getSelectedIndex() != row)
      list->setSelectedIndex(row);
  if (UiText *title = dynamic_cast<UiText *>(z->getElementById(28002)))
    title->setText(t.title);
  if (UiText *body = dynamic_cast<UiText *>(z->getElementById(28005)))
    this->helpRaw = windows1252ToUtf8(text);
  // (wrapped to the box when it's next drawn, wrapHelp)
  this->helpWrapPending = true;
}

// ours: a text box with lines of its own scrolls them without wrapping, so the
// topic's paragraphs are broken at word breaks to the box's width (292, less
// its scroll bar), each line measured in the box's own font (help.lyt MainText
// font 25003, size 25004)
void UiGameScreen::wrapHelp(SDL_Renderer *renderer) {
  if (!this->helpWrapPending || !this->helpPanel)
    return;
  UiText *body = dynamic_cast<UiText *>(this->helpPanel->getElementById(28005));
  if (!body)
    return;
  this->helpWrapPending = false;
  const int room = 272;
  auto width = [&](const std::string &s) {
    int w = 0, h = 0;
    if (SDL_Texture *t = this->resource_manager->getStringTexture(renderer, 25003, s, SDL_Color{255, 255, 255, 255}, 25004))
      this->resource_manager->getTextSize(t, &w, &h);
    return w;
  };
  std::string out;
  std::istringstream paragraphs(this->helpRaw);
  std::string paragraph;
  while (std::getline(paragraphs, paragraph)) {
    std::istringstream words(paragraph);
    std::string word, line;
    while (words >> word) {
      const std::string candidate = line.empty() ? word : line + " " + word;
      if (!line.empty() && width(candidate) > room) {
        out += line + "\n";
        line = word;
      } else {
        line = candidate;
      }
    }
    out += line + "\n";
  }
  body->setText(out);
}

// A topic picked in the list shows (the list's click handler)
void UiGameScreen::refreshHelp() {
  if (!this->helpPanel || !this->isPanelOpen(38)) {
    this->helpShown = -1;
    return;
  }
  if (UiListBox *list = dynamic_cast<UiListBox *>(this->helpPanel->getElementById(28003))) {
    const int row = list->getSelectedIndex();
    if (row >= 0 && row != this->helpShown)
      this->showHelpTopic(row);
  }
}

std::string UiGameScreen::helpTopicText() const {
  return this->helpRaw;
}

void UiGameScreen::setupAnimalPanel() {
  for (Entry &e : this->entries)
    if (e.panel && e.id == 6)
      this->animalPanel = e.layout;
  UiLayout *z = this->animalPanel;
  if (!z)
    return;
  if (UiElement *name = z->getElementById(3102))
    name->setHidden(false);
  // The layout hides Zookeeper Recommendations (state=2048); the game
  // shows it. Track starts off.
  if (UiButton *keeper = dynamic_cast<UiButton *>(z->getElementById(3122))) {
    keeper->setHidden(false);
    // (a plain button in the original, not a lit toggle)
    keeper->setToggle(false);
    keeper->setToggledOn(false);
  }
  if (UiButton *track = dynamic_cast<UiButton *>(z->getElementById(3114)))
    track->setToggledOn(false);
  if (UiButton *b = dynamic_cast<UiButton *>(z->getElementById(3115)))
    b->onClick = [this] {
      if (this->animalRequest && this->shownAnimal >= 0)
        this->animalRequest(AnimalRequest::PickUp, this->shownAnimal);
    };
  // Exhibit Information (zoo.exe infoanm_HabitatInfoButton_click 0x618c02:
  // an animal in an exhibit - its +0x2c clear - opens the exhibit's panel)
  if (UiButton *b = dynamic_cast<UiButton *>(z->getElementById(3197)))
    b->onClick = [this] {
      const Animals::Member *m = this->animals ? this->animals->member(this->shownAnimal) : nullptr;
      if (m && m->exhibit >= 0)
        this->showExhibit(m->exhibit);
    };
  // (a toggle, state=2048, without a lit frame: on, zoo.exe
  // infoanm_KeeperPlaque_click 0x483b21 -> 0x4831b2 fills and shows the
  // Recommendations; off, infoanm_KeeperPlaque_off 0x48330d -> 0x4440f1
  // hides them, uiFind(0x1c) slot 0x48; their own close lets the toggle go,
  // keprinfo_Panel_hide 0x4832f9 slot 0x54 - so it's on while they're open,
  // and a second click shuts them)
  if (UiButton *b = dynamic_cast<UiButton *>(z->getElementById(3122)))
    b->onClick = [this] {
      if (this->isPanelOpen(28))
        this->setPanelOpen(28, false);
      else if (this->shownAnimal >= 0)
        this->showAdvice(this->shownAnimal);
    };
  if (UiButton *b = dynamic_cast<UiButton *>(z->getElementById(3111)))
    b->onClick = [this] {
      const Animals::Member *m = this->animals ? this->animals->member(this->shownAnimal) : nullptr;
      if (!m)
        return;
      int id = m->id;
      // (zoo.exe removeObject 0x50a0b0: an animal always asked, while
      // confirmSell is on; the refund cut, not rounded)
      auto sell = [this, id] {
        if (this->animalRequest)
          this->animalRequest(AnimalRequest::Sell, id);
        if (this->shownAnimal == id) {
          this->shownAnimal = -1;
          this->setPanelOpen(6, false);
        }
      };
      if (Original::confirmSell())
        this->askConfirm(Original::sellQuestion(this->resource_manager, m->name, static_cast<float>(this->animals->sellValue(id))), sell);
      else
        sell();
    };
  if (UiText *name = dynamic_cast<UiText *>(z->getElementById(3102)))
    name->onCommit = [this](const std::string &typed) {
      // (zoo.exe infoanm_AnimalName_click 0x618b27: setName, 0x4e1d6b ->
      // 0x449215; the lists sort by name again)
      if (!this->animals || !Original::nameTaken(typed))
        return;
      if (Animals::Member *m = this->animals->member(this->shownAnimal)) {
        m->name = typed;
        // (0x4e1d6b's unitSetName 0x449515: the rename cheats, 0x44940b)
        if (this->animalRenamed)
          this->animalRenamed(this->shownAnimal, typed);
      }
    };
}

void UiGameScreen::showBuilding(int id) {
  PlacedObjects::Object *o = this->objects ? this->objects->byId(id) : nullptr;
  if (!o)
    return;
  if(this->shows)this->shows->refresh();
  bool commerce = o->price >= 0;
  UiLayout *z = nullptr;
  for (Entry &e : this->entries)
    if (e.panel && e.id == (commerce ? 21 : 20))
      z = e.layout;
  if (!z)
    return;
  this->openInfo(commerce ? 21 : 20);
  this->shownBuilding = id;
  int nameId = commerce ? 4502 : 4602;
  if (UiElement *n = z->getElementById(nameId))
    n->setHidden(false);
  // Its name typed into (zoo.exe infocom_Buildingname_click 0x61750f,
  // infoncom_Buildingname_click 0x617f1b: the selected object's setName,
  // 0x449215 - kept only with something to see in it)
  if (UiText *n = dynamic_cast<UiText *>(z->getElementById(nameId)))
    n->onCommit = [this](const std::string &typed) {
      if (!this->objects || !Original::nameTaken(typed))
        return;
      if (PlacedObjects::Object *b = this->objects->byId(this->shownBuilding))
        b->label = typed;
    };
  // (no move hand on a building's panel, as the original)
  for (int hid : commerce ? std::vector<int>{4580} : std::vector<int>{})
    if (UiElement *hand = z->getElementById(hid))
      hand->setHidden(true);
  // Its picture (cInfoImageName: scenery/building/inbldg for most)
  if (UiImage *pic = dynamic_cast<UiImage *>(z->getElementById(commerce ? 4550 : 4650))) {
    std::string img = "scenery/building/inbldg/inbldg";
    if (IniReader *ai = this->resource_manager->getIniReader(PlacedObjects::fileOf(*o))) {
      std::string v = ai->get("characteristics/strings", "cinfoimagename");
      if (!v.empty())
        img = v;
      delete ai;
    }
    pic->setImage(img);
    pic->setHidden(false);
  }
  if (UiButton *tab = dynamic_cast<UiButton *>(z->getElementById(commerce ? 4575 : 4675)))
    tab->choose();
  z->syncTabs();
  if (commerce) {
    // Price up / down, $0.25 a step; sell
    // (zoo.exe ZTBuilding::raisePrice 0x611b43: no more than economy.cfg's
    // cBuildingUseCostMax ($30) nor its cHighCost (type +0x184);
    // lowerPrice 0x5a5bbc: no less than $0 nor its cLowCost (type +0x17c))
    float lo = 1, hi = 1;
    if (IniReader *ai = this->resource_manager->getIniReader(PlacedObjects::fileOf(*o))) {
      std::string l = ai->get("characteristics/floats", "clowcost"), h = ai->get("characteristics/floats", "chighcost");
      lo = l.empty() ? 1.0f : static_cast<float>(std::atof(l.c_str()));
      hi = h.empty() ? 1.0f : static_cast<float>(std::atof(h.c_str()));
      delete ai;
    }
    this->shownLowCost = lo;
    this->shownHighCost = hi;
    if (UiButton *up = dynamic_cast<UiButton *>(z->getElementById(4557)))
      up->onClick = [this, hi] {
        if (PlacedObjects::Object *b = this->objects->byId(this->shownBuilding))
          if(!this->shows || !this->shows->setStandAdmission(b->id,b->price+Original::priceStep))
            b->price = std::min({b->price + Original::priceStep, Original::economy().buildingUseCostMax, hi});
      };
    if (UiButton *dn = dynamic_cast<UiButton *>(z->getElementById(4556)))
      dn->onClick = [this, lo] {
        if (PlacedObjects::Object *b = this->objects->byId(this->shownBuilding))
          if(!this->shows || !this->shows->setStandAdmission(b->id,b->price-Original::priceStep))
            b->price = std::max({b->price - Original::priceStep, 0.0f, lo});
      };
  }
  if(commerce)if(auto *price=dynamic_cast<UiText*>(z->getElementById(4555))) {
    price->onBeginEdit=[this]{auto *b=this->objects->byId(this->shownBuilding);return b?std::to_string(b->price):"0";};
    // zoo.exe infocom_ItemcostCost_click 0x6176c1: the typed text's
    // currency symbol taken out once (text 22100, initLocale 0x5359a0's
    // 0x63b3c4: 0x5fe02d) and every thousands separator (0x63b3b8: 0x5fe0a9,
    // ","), the rest
    // read as a number (0x4b40ae); under the price, lowerPrice 0x5a5bbc by
    // the difference (no less than $0 nor cLowCost), over it raisePrice
    // 0x611b43 (no more than cBuildingUseCostMax nor cHighCost) - so a typed
    // price is kept to the arrows' bounds, one way only
    price->onCommit=[this](const std::string &v) {
      auto *b=this->objects->byId(this->shownBuilding);
      std::string text=v;
      const std::string sign=this->resource_manager->getString(22100);
      if(size_t at=sign.empty()?std::string::npos:text.find(sign);at!=std::string::npos)text.erase(at,sign.size());
      for(size_t at;(at=text.find(','))!=std::string::npos;)text.erase(at,1);
      const float value=static_cast<float>(std::atof(text.c_str()));
      if(!b || !std::isfinite(value))return;
      if(this->shows && this->shows->setStandAdmission(b->id,value))return;
      float lo=1,hi=1;
      if(auto *ai=this->resource_manager->getIniReader(PlacedObjects::fileOf(*b))) {
        lo=std::atof(ai->get("characteristics/floats","clowcost","1").c_str());
        hi=std::atof(ai->get("characteristics/floats","chighcost","1").c_str());delete ai;
      }
      if(value<b->price)b->price=std::max({value,0.0f,lo});
      else if(value>b->price)b->price=std::min({value,Original::economy().buildingUseCostMax,hi});
    };
  }
  if (UiButton *sell = dynamic_cast<UiButton *>(z->getElementById(commerce ? 4516 : 4616)))
    sell->onClick = [this, commerce] {
      int id = this->shownBuilding;
      auto sell = [this, commerce, id] {
        if (this->sellBuilding)
          this->sellBuilding(id);
        if (this->shownBuilding == id) {
          this->setPanelOpen(commerce ? 21 : 20, false);
          this->shownBuilding = -1;
        }
      };
      // (zoo.exe infoSellOrFire_click 0x48d0b4, the info panels' Sell and
      // Fire, -> sellSelected 0x48d052 -> removeSelected 0x48d065: the
      // view's selected thing, no longer the hovered one, sellObject 0x50a432
      // -> removeObject 0x50a0b0: a building
      // - cNeedsConfirm - asked first, "Are you sure you want to sell %s?
      // Refund: $N", its 80%)
      const PlacedObjects::Object *b = this->objects ? this->objects->byId(id) : nullptr;
      if (b && Original::sellNeedsConfirm(this->resource_manager, PlacedObjects::fileOf(*b))) {
        float value = 0;
        for (const CatalogItem &item : ItemCatalog::get().all())
          if (item.file == PlacedObjects::fileOf(*b))
            value = item.cost * Original::sellFactor;
        this->askConfirm(Original::sellQuestion(this->resource_manager, b->label.empty() ? b->name : b->label, value), sell);
      } else {
        sell();
      }
    };
  this->refreshBuilding();
}

void UiGameScreen::setupColours() {
  UiLayout *paint = nullptr, *picker = nullptr;
  for (Entry &e : this->entries) {
    if (e.panel && e.id == 302)
      paint = e.layout;
    if (e.panel && e.id == 37)
      picker = e.layout;
  }
  if (paint)
    for (int part : {0, 1})
      if (UiButton *b = dynamic_cast<UiButton *>(paint->getElementById(2461 + part)))
        b->onClick = [this, part] {
          this->colourPart = part;
          this->setPanelOpen(302, false);
          this->setPanelOpen(37, true);
        };
  if (picker)
    for (int k = 0; k < 24; k++)
      if (UiButton *b = dynamic_cast<UiButton *>(picker->getElementById(2401 + k))) {
        b->setToggle(false);
        b->onClick = [this, k] {
          PlacedObjects::Object *o = this->objects ? this->objects->byId(this->shownBuilding) : nullptr;
          if (o && k < static_cast<int>(this->colourChoices.size()))
            this->objects->setColour(*o, this->colourPart, this->colourChoices[k]);
          this->setPanelOpen(37, false);
          this->setPanelOpen(302, true);
        };
      }
}

// The paint tab (measured against the original's hot dog stand: "Roof"
// and its colour; clicked, the panel shows the 24 colours, ordered by
// red, then green, then blue; a pick paints it and the tab comes back).
// Only buildings that can be painted (cIsColorReplaced) have the tab.
// zoo.exe infocomC_Panel_show 0x4882ac: for each of the building's parts
// (+0x28..+0x2c, 12 bytes each) the button 2461 + n (0x99d) filled with its
// colour and the label 2451 + n (0x993) its title (+0x224), both shown;
// the parts it hasn't, of two, hidden and switched off (flag 0x10)
void UiGameScreen::refreshColours(UiLayout *z, PlacedObjects::Object *o, bool commerce) {
  UiLayout *paint = nullptr, *picker = nullptr;
  for (Entry &e : this->entries) {
    if (e.panel && e.id == 302)
      paint = e.layout;
    if (e.panel && e.id == 37)
      picker = e.layout;
  }
  const PlacedObjects::Colouring *c = o ? this->objects->colouringOf(PlacedObjects::fileOf(*o)) : nullptr;
  UiButton *tab = z ? dynamic_cast<UiButton *>(z->getElementById(commerce ? 4577 : 4677)) : nullptr;
  if (tab) {
    tab->setHidden(!c);
    if (!c && tab->isToggledOn())
      if (UiButton *first = dynamic_cast<UiButton *>(z->getElementById(commerce ? 4575 : 4675))) {
        first->choose();
        z->syncTabs();
      }
  }
  bool on = c && tab && tab->isToggledOn();
  if (!on) {
    this->setPanelOpen(302, false);
    this->setPanelOpen(37, false);
    return;
  }
  if (!this->isPanelOpen(37))
    this->setPanelOpen(302, true);
  if (paint) {
    for (int part = 0; part < 2; part++) {
      bool has = part < static_cast<int>(c->parts.size());
      if (UiText *label = dynamic_cast<UiText *>(paint->getElementById(2451 + part))) {
        label->setHidden(!has);
        if (has)
          label->setText(this->resource_manager->getString(c->parts[part].title));
      }
      if (UiButton *b = dynamic_cast<UiButton *>(paint->getElementById(2461 + part))) {
        b->setHidden(!has);
        int pick = has ? this->objects->colourOf(*o, part) : -1;
        if (pick >= 0)
          b->setFillColor(c->parts[part].swatches[pick]);
      }
    }
  }
  if (picker && this->isPanelOpen(37) && this->colourPart < static_cast<int>(c->parts.size())) {
    const PlacedObjects::ColourPart &part = c->parts[this->colourPart];
    std::vector<int> order(part.swatches.size());
    for (size_t i = 0; i < order.size(); i++)
      order[i] = static_cast<int>(i);
    std::stable_sort(order.begin(), order.end(), [&](int a, int b) {
      const SDL_Color &x = part.swatches[a], &y = part.swatches[b];
      return std::tie(x.r, x.g, x.b) < std::tie(y.r, y.g, y.b);
    });
    this->colourChoices = order;
    for (int k = 0; k < 24; k++)
      if (UiButton *b = dynamic_cast<UiButton *>(picker->getElementById(2401 + k))) {
        bool has = k < static_cast<int>(order.size());
        b->setHidden(!has);
        if (has)
          b->setFillColor(part.swatches[order[k]]);
      }
  }
}

// zoo.exe infocom_CloseWindow_click 0x4881c7 and infoncom_CloseWindow_click
// 0x617efb (the close button of panel 21, 20: uiFind 0x15, 0x14, hidden, and
// nothing is selected, 0x4ef75b with none)
// zoo.exe buildingInfo_Panel_hide 0x4881a9 (infocom's and infoncom's):
// shut, the paint tab goes with it (uiFind 0x12e, slot 0x48), and so the
// colour picker (infocomC_Panel_hide 0x488291: uiFind 0x25, slot 0x48)
void UiGameScreen::refreshBuilding() {
  if (this->shownBuilding < 0 || !this->objects)
    return;
  PlacedObjects::Object *o = this->objects->byId(this->shownBuilding);
  bool open21 = this->isPanelOpen(21), open20 = this->isPanelOpen(20);
  if (!o || (!open21 && !open20)) {
    this->shownBuilding = -1;
    this->setPanelOpen(21, false);
    this->setPanelOpen(20, false);
    this->setPanelOpen(302, false);
    this->setPanelOpen(37, false);
    return;
  }
  UiLayout *z = nullptr;
  for (Entry &e : this->entries)
    if (e.panel && e.id == (open21 ? 21 : 20))
      z = e.layout;
  if (!z)
    return;
  auto text = [&](int eid, const std::string &v) {
    if (UiText *e = dynamic_cast<UiText *>(z->getElementById(eid)))
      if (!e->isBeingEdited())
        e->setText(v);
  };
  if(auto *linked=dynamic_cast<UiText*>(z->getElementById(71110))) {
    const bool stand=this->shows && this->shows->isStand(o->id);linked->setHidden(!stand);
    if(stand) {
      const int id=this->shows->standShow(o->id);const auto *tank=this->fences?this->fences->exhibit(id):nullptr;
      linked->setText(tank?"Show: "+tank->name:"Face a show tank to link");
    }
  }
  this->refreshColours(z, o, open21);
  int months = buildingMonthsOpen(this->sim, this->fences, this->monthNumber, *o);
  if (open21) {
    text(4502, o->label);
    char p[32];
    std::snprintf(p, sizeof p, "$%.2f", o->price);
    text(4555, p);
    // The arrows greyed out at the ends (zoo.exe onPriceUp 0x6175e6 /
    // onPriceDown 0x5a5bfd: the up arrow's off when canRaisePrice 0x488adc -
    // its cHighCost, if not 0, no more than the price, or economy.cfg's
    // cBuildingUseCostMax under it - the down arrow's when canLowerPrice
    // 0x488b2d - its cLowCost, if not 0, no less than the price, or the
    // price $0 or under). A show's stand goes by its show's limits.
    if (!this->shows || !this->shows->isStand(o->id)) {
      const float hi = this->shownHighCost, lo = this->shownLowCost;
      if (UiButton *up = dynamic_cast<UiButton *>(z->getElementById(4557)))
        up->setDisabled((hi != 0.0f && hi <= o->price) || Original::economy().buildingUseCostMax < o->price);
      if (UiButton *dn = dynamic_cast<UiButton *>(z->getElementById(4556)))
        dn->setDisabled((lo != 0.0f && lo >= o->price) || o->price <= 0.0f);
    }
    // (zoo.exe buildingInfoNumbers 0x488446: income and upkeep each cut to
    // whole dollars, the profit the one less the other, a month's each of
    // them divided as whole numbers, all through moneyToText 0x40eca1 with
    // the thousands grouped - "$1,234")
    const long in = static_cast<long>(o->income), up = static_cast<long>(o->upkeep);
    auto grouped = [](long v) { return v < 0 ? "-" + formatPrice(static_cast<int>(-v)) : formatPrice(static_cast<int>(v)); };
    text(4531, grouped(in));
    text(4533, grouped(up));
    text(4535, grouped(in - up));
    text(4559, grouped(in / months));
    text(4561, grouped(up / months));
    text(4591, grouped((in - up) / months));
    text(4573, std::to_string(months));
    // Items Sold: each thing it sold, its icon
    int slot = 0;
    for (const auto &[item, n] : o->sold) {
      if (slot >= 5)
        break;
      if (UiImage *img = dynamic_cast<UiImage *>(z->getElementById(4567 + slot))) {
        std::string icon;
        if (IniReader *ic = this->resource_manager->getIniReader("items/" + item + ".cfg")) {
          icon = ic->get("characteristics", "icon");
          delete ic;
        }
        if (!icon.empty() && img->getImagePath() != icon)
          img->setImage(icon);
        img->setHidden(icon.empty() || n <= 0);
      }
      slot++;
    }
    for (; slot < 5; slot++)
      if (UiElement *img = z->getElementById(4567 + slot))
        img->setHidden(true);
  } else {
    text(4602, o->label);
    text(4668, std::to_string(months));
    // Programs (an animal house: zoo.exe 0x61794a): up to five, cheapest
    // first, each its picture; the picked one's name, upkeep and the
    // happiness it gives adults and children ("+5")
    {
      std::vector<const ResearchProgram *> shows = o->nameId ? Research::get().collectionFor(o->nameId)
                                                             : std::vector<const ResearchProgram *>{};
      if (UiElement *tab = z->getElementById(4678))
        tab->setHidden(shows.empty());
      if (!shows.empty()) {
        o->program = std::clamp(o->program, 0, static_cast<int>(shows.size()) - 1);
        for (int k = 0; k < 5; k++)
          if (UiButton *b = dynamic_cast<UiButton *>(z->getElementById(4631 + k))) {
            bool has = k < static_cast<int>(shows.size());
            b->setHidden(!has);
            if (has) {
              Animation *pic = shows[k]->entityIcon.empty() ? nullptr
                                                            : this->resource_manager->getAnimation(shows[k]->entityIcon);
              b->setIcon(pic);
              b->setToggledOn(k == o->program);
              int id = o->id;
              // (zoo.exe infoncm2_Buttons_click 0x617ff2: setProgram 0x611b9f
              // with the button less 4631 - +0x188 set only when under the
              // type's program count, +0x168 - its button; then its upkeep
              // again in 4642 and 4696, as the refresh below)
              b->onClick = [this, id, k] {
                if (PlacedObjects::Object *h = this->objects ? this->objects->byId(id) : nullptr)
                  h->program = k;
              };
            }
          }
        const ResearchProgram *p = shows[o->program];
        text(4640, p->name);
        text(4642, formatCents(static_cast<long>(p->effectVal[0]) * 100));
        text(4696, formatCents(static_cast<long>(p->effectVal[0]) * 100));
        text(4644, "+" + std::to_string(p->effectVal[1]));
        text(4646, "+" + std::to_string(p->effectVal[2]));
      } else if (UiButton *status = dynamic_cast<UiButton *>(z->getElementById(4675))) {
        if (UiButton *tab = dynamic_cast<UiButton *>(z->getElementById(4678)))
          if (tab->isToggledOn()) {
            status->choose();
            z->syncTabs();
          }
      }
    }
    // Visitors: last month, this month, all told
    text(4664, std::to_string(o->visitorsLast));
    text(4666, std::to_string(o->visitorsNow));
    text(4662, std::to_string(o->visitorsTotal));
    // Upkeep Cost: its cUpkeep a month (ZUPDATE's infoncm1.lyt 4695/4696)
    {
      static std::map<std::string, double> upkeepOf;
      std::string file = PlacedObjects::fileOf(*o);
      auto u = upkeepOf.find(file);
      if (u == upkeepOf.end()) {
        double up = 0;
        if (IniReader *ai = this->resource_manager->getIniReader(file)) {
          std::string v = ai->get("characteristics/floats", "cupkeep");
          up = v.empty() ? 0.0 : std::atof(v.c_str());
          delete ai;
        }
        u = upkeepOf.emplace(file, up).first;
      }
      text(4696, formatCents(std::lround(u->second * 100)));
    }
  }
}

void UiGameScreen::showGuest(int id) {
  if (!this->guests || !this->guests->guest(id))
    return;
  UiLayout *z = nullptr;
  for (Entry &e : this->entries)
    if (e.panel && e.id == 9)
      z = e.layout;
  if (!z)
    return;
  this->openInfo(9);
  this->shownGuest = id;
  this->guests->selected = id;
  this->guestThoughts = static_cast<size_t>(-1);
  if (UiElement *name = z->getElementById(3402))
    name->setHidden(false);
  // Its name typed into (zoo.exe infogst_GuestName_click 0x618a35: the
  // selected guest's setName, 0x449215 - kept only with something to see);
  // setName (class slot 0x1c, 0x449515) runs the rename cheats too (0x44940b)
  if (UiText *name = dynamic_cast<UiText *>(z->getElementById(3402)))
    name->onCommit = [this](const std::string &typed) {
      if (!this->guests || !Original::nameTaken(typed))
        return;
      if (Guests::Guest *g = this->guests->guest(this->shownGuest))
        g->name = typed;
      if (this->guestRenamed)
        this->guestRenamed(this->shownGuest, typed);
    };
  // (Track starts off: on, its yellow lit art filled the portrait's circle)
  if (UiButton *track = dynamic_cast<UiButton *>(z->getElementById(3415)))
    track->setToggledOn(false);
  // Its tab: the one last picked (zoo.exe infogst_Panel_show 0x46a91b: the
  // radio set 3460's current tab shown again; only after a zoo is loaded -
  // 0x4c735c sets 0x63e47c - the one it had first, Status 3462, state=8 in
  // infogst.lyt. ours: each loaded zoo has a new screen, its tabs as the
  // layout has them)
  // PickupGuest (zoo.exe infoPickUp_click 0x46af10 -> 0x46ab75): the panel
  // shut, the guest held at the cursor
  if (UiButton *b = dynamic_cast<UiButton *>(z->getElementById(3416)))
    b->onClick = [this] {
      const int id = this->shownGuest;
      if (id < 0 || !this->guestPickUp)
        return;
      this->setPanelOpen(9, false);
      this->shownGuest = -1;
      this->guestPickUp(id);
    };
  z->syncTabs();
  this->refreshGuest();
}

void UiGameScreen::refreshGuest() {
  if (!this->guests)
    return;
  UiLayout *z = nullptr;
  for (Entry &e : this->entries)
    if (e.panel && e.id == 9)
      z = e.layout;
  const Guests::Guest *g = this->guests->guest(this->shownGuest);
  if (this->isPanelOpen(9) && !g)
    this->setPanelOpen(9, false);
  if (!this->isPanelOpen(9) && this->shownGuest >= 0) {
    if (this->guests->selected == this->shownGuest)
      this->guests->selected = -1;
    this->shownGuest = -1;
  }
  if (!z || !g || !this->isPanelOpen(9))
    return;
  auto text = [&](int eid, const std::string &v) {
    if (UiText *e = dynamic_cast<UiText *>(z->getElementById(eid)))
      if (!e->isBeingEdited())
        e->setText(v);
  };
  auto bar = [&](int eid, float v) {
    if (UiStatusImage *e = dynamic_cast<UiStatusImage *>(z->getElementById(eid)))
      e->setValue(static_cast<int>(std::lround(v)));
  };
  text(3402, g->name);
  // zoo.exe guestInfoRefresh 0x46a144: each a level 0-100 against the
  // type's limits, in whole numbers (guestHappinessLevel 0x46a606,
  // guestThirstLevel 0x46a655, guestHungerLevel 0x46a6a4, guestEnergyLevel
  // 0x46a6f3, guestBathroomLevel 0x46a742: (value - least) x 100 / (most -
  // least)); the needs shown as 100 less it
  auto level = [](float v, float lo, float hi) {
    return (static_cast<int>(v) - static_cast<int>(lo)) * 100 / (static_cast<int>(hi) - static_cast<int>(lo));
  };
  bar(3404, static_cast<float>(level(g->happiness, Original::guestHappinessMin, Original::guestHappinessMax)));
  bar(3406, static_cast<float>(100 - level(g->thirst, Original::guestNeedMin, Original::guestNeedMax)));
  bar(3408, static_cast<float>(100 - level(g->hunger, Original::guestNeedMin, Original::guestNeedMax)));
  bar(3410, static_cast<float>(100 - level(g->bathroom, Original::guestNeedMin, Original::guestNeedMax)));
  bar(3412, static_cast<float>(100 - level(g->tired, Original::guestNeedMin, Original::guestNeedMax)));
  text(3475, this->resource_manager->getString(this->guests->timeInParkText(*g)));
  text(3476, this->animalName && g->favourite >= 0 ? this->animalName(g->favourite) : "");
  if (UiListBox *l = dynamic_cast<UiListBox *>(z->getElementById(3469)))
    if (this->guestThoughts != g->thoughts.size() || l->getItemCount() != g->thoughts.size()) {
      l->setWrapped(true);
      l->clear();
      for (const std::string &t : g->thoughts)
        l->addItem(t, SDL_Color{255, 255, 255, 255});
      this->guestThoughts = g->thoughts.size();
    }
}

void UiGameScreen::showAnimal(int id) {
  if (!this->animalPanel || !this->animals || !this->animals->member(id))
    return;
  this->openInfo(6);
  this->shownAnimal = id;
  // With the Exhibit/Show List open, the list picks its exhibit
  if (this->isPanelOpen(30))
    if (const Animals::Member *m = this->animals->member(id))
      if (m->exhibit >= 0 && m->exhibit != this->shownExhibit)
        if (UiListBox *list = this->exhibitPanel
                                  ? dynamic_cast<UiListBox *>(this->exhibitPanel->getElementById(4305))
                                  : nullptr)
          for (size_t i = 0; i < this->listedExhibits.size(); i++)
            if (this->listedExhibits[i] == m->exhibit) {
              list->setSelectedIndex((int)i);
              auto centre = this->centreOn;
              this->centreOn = nullptr;
              this->refreshExhibits();
              this->centreOn = centre;
            }
  this->animals->selected = id;
  // It opens on Status (its tabs, zoo.exe infoanm_Buttons_click 0x4ee81b:
  // the page changed and the panel refreshed, nothing more)
  if (UiButton *tab = dynamic_cast<UiButton *>(this->animalPanel->getElementById(3162)))
    tab->choose();
  this->animalPanel->syncTabs();
  this->refreshAnimal();
}

int UiGameScreen::animalBarValue(int elementId) const {
  const UiStatusImage *e =
      this->animalPanel ? dynamic_cast<const UiStatusImage *>(this->animalPanel->getElementById(elementId)) : nullptr;
  return e ? e->getValue() : -1;
}

int UiGameScreen::animalBarBand(int elementId) const {
  const UiStatusImage *e =
      this->animalPanel ? dynamic_cast<const UiStatusImage *>(this->animalPanel->getElementById(elementId)) : nullptr;
  return e ? static_cast<int>(e->band()) : -1;
}

void UiGameScreen::refreshAnimal() {
  if (!this->animals)
    return;
  UiLayout *z = this->animalPanel;
  const Animals::Member *m = this->animals->member(this->shownAnimal);
  if (this->isPanelOpen(6) && !m)
    this->setPanelOpen(6, false);
  // (shut, nothing's selected: zoo.exe infoanm_CloseWindow_click 0x4ed80a,
  // ui_main_4b2322 with none)
  if (!this->isPanelOpen(6) && this->shownAnimal >= 0) {
    if (this->animals->selected == this->shownAnimal)
      this->animals->selected = -1;
    this->shownAnimal = -1;
  }
  if (!z || !m || !this->isPanelOpen(6))
    return;
  auto text = [&](int eid, const std::string &v) {
    if (UiText *e = dynamic_cast<UiText *>(z->getElementById(eid)))
      if (!e->isBeingEdited())
        e->setText(v);
  };
  auto bar = [&](int eid, float v) {
    if (UiStatusImage *e = dynamic_cast<UiStatusImage *>(z->getElementById(eid)))
      e->setValue(static_cast<int>(std::lround(v)));
  };
  text(3102, m->name);
  // (zoo.exe infoanm_Panel_show 0x4ee665, an animal picked: its picture and
  // name, 0xc2a and 0xc1e, the bars refreshed, 0x4673b5, the plaque's
  // picture by its habitat's kind - 0x251c, 0x254f, 0x2550: 0, 1, 2 - and
  // the tab it was left on, the layout's first after a zoo starts, 0x63e418)
  // Its carer's (cKeeper - zoo.exe 0x4831b2): the plaque's picture and help
  // (Zookeeper, Scientist or Marine Specialist Recommendations), the
  // hunger and suitability icons' help
  {
    const int carer = this->animals->carerIndex(*m);
    if (carer != this->animalCarer) {
      if (UiButton *plaque = dynamic_cast<UiButton *>(z->getElementById(3122)))
        plaque->chooseAnimation(static_cast<size_t>(carer));
      this->animalCarer = carer;
    }
    for (int eid : {3122, 3107, 3109})
      if (UiElement *e = z->getElementById(eid))
        e->chooseHelp(static_cast<size_t>(carer));
  }
  // zoo.exe animalInfoRefresh 0x4673b5: happiness (animalHappinessLevel
  // 0x467366), 100 less the hunger level (animalHungerLevel 0x4678bf),
  // health (animalHealthLevel 0x4679c5) - each (value - least) x 100 /
  // (most - least) - and the exhibit's rating; an egg (+0x395) shows 100 for
  // all but its health
  bar(3104, m->egg ? 100.0f : static_cast<float>(Animals::shownHappiness(*m)));
  bar(3108, m->egg ? 100.0f : m->food);
  bar(3106, static_cast<float>(this->animals->shownHealth(*m)));
  bar(3110, m->egg ? 100.0f
                   : this->animalSuitability ? static_cast<float>(this->animalSuitability(m->id)) : 0.0f);
  // The exhibit bar's colours by the animal's own limits (0x4673b5 sets its
  // two transitions through 0x467963, around the rating v): red while v x
  // 100 is under cPctHabitat x cHabitatPreference (0x46790e) - v+1, v+2;
  // else yellow while v is under cHabitatPreference (0x418b8c, not for an
  // egg) - v-1, v+1; else green - v-2, v-1. An egg's bar shows 100 against
  // the same transitions. (0x418b8c's +0x142/+0x143 not modelled)
  if (UiStatusImage *e = dynamic_cast<UiStatusImage *>(z->getElementById(3110));
      e && this->animalSuitability && m->type >= 0 && m->type < static_cast<int>(this->animals->types().size())) {
    const Animals::Type &t = this->animals->types()[m->type];
    const int v = this->animalSuitability(m->id);
    if (v * 100 < t.pctHabitat * t.habitatPreference)
      e->setTransitions({v + 1, v + 2});
    else if (!m->egg && v < t.habitatPreference)
      e->setTransitions({v - 1, v + 1});
    else
      e->setTransitions({v - 2, v - 1});
  }
  if (UiImage *sex = dynamic_cast<UiImage *>(z->getElementById(3172))) {
    std::string want = m->female ? "ui/animal/icfemale.tga" : "ui/animal/icmale.tga";
    if (this->animalSexImage != want) {
      sex->setImage(want);
      this->animalSexImage = want;
    }
  }
  // Thoughts: the latest five guests had about it (zoo.exe animalInfoList
  // 0x467a33: the list 0xc35 - 3125 - cleared, 0x467b37(animal, 5): the
  // zoo's thought list from its newest, those about this animal (entry
  // +0x24), at most 5, each a row as text)
  if (this->guests)
    if (UiListBox *l = dynamic_cast<UiListBox *>(z->getElementById(3125))) {
      std::vector<std::string> th = this->guests->thoughtsAbout(m->id, -1, 5);
      if (l->getItemCount() != th.size() || (!th.empty() && l->getItemCount() && th.front() != this->animalTopThought)) {
        l->setWrapped(true);
        l->clear();
        for (const std::string &t : th)
          l->addItem(t, SDL_Color{255, 255, 255, 255});
        this->animalTopThought = th.empty() ? "" : th.front();
      }
    }
  double now = this->animals->clock();
  text(3177, this->resource_manager->getString(Animals::agoText(now - m->lastAte, false)));
  text(3179, this->resource_manager->getString(Animals::agoText(now - m->lastSlept, true)));
}

// Zookeeper Recommendations (measured in the original): the animal's name
// over a list of what would make it happier, problems in red (zoo.exe
// keprinfo_Panel_show 0x483b71: the animal kept at 0x643194 - its name,
// +0x108, into 26001, the list 26002 filled)
void UiGameScreen::showAdvice(int id) {
  const Animals::Member *m = this->animals ? this->animals->member(id) : nullptr;
  UiLayout *z = nullptr;
  for (Entry &e : this->entries)
    if (e.panel && e.id == 28)
      z = e.layout;
  if (!m || !z)
    return;
  // (the animal plaque hidden: keprinfo_Panel_show 0x483b71 -> 0x4440d6)
  this->setPanelOpen(27, false);
  this->setPanelOpen(28, true);
  // The layout lists the zookeeper's, the scientist's and the marine
  // specialist's backgrounds and titles: the animal's carer's (cKeeper -
  // zoo.exe 0x4831b2 picks 26000's picture and 26020's text by it)
  const int carer = this->animals->carerIndex(*m);
  static const char *const backs[3] = {"ui/keprinfo/kinfobk/kinfobk", "ui/sciinfo/scinfobk/scinfobk",
                                       "ui/trnrinfo/tinfobk/tinfobk"};
  if (UiImage *bk = dynamic_cast<UiImage *>(z->getElementById(26000)))
    bk->setImage(backs[carer]);
  if (UiText *title = dynamic_cast<UiText *>(z->getElementById(26020)))
    title->setText(this->resource_manager->getString(26020 + carer));
  if (UiText *name = dynamic_cast<UiText *>(z->getElementById(26001)))
    name->setText(m->name);
  if (UiListBox *list = dynamic_cast<UiListBox *>(z->getElementById(26002))) {
    list->setWrapped(true);
    list->clear();
    if (this->animalAdvice)
      for (const auto &[text, warning] : this->animalAdvice(id))
        list->addItem(text, warning ? SDL_Color{255, 0, 0, 255} : SDL_Color{0, 0, 0, 255});
  }
}

// The round portrait at the panel's top left: the animal itself
void UiGameScreen::drawAnimalPortrait(SDL_Renderer *renderer) {

  // (a guest's, the same way)
  if (this->guests && this->isPanelOpen(9))
    if (const Guests::Guest *g = this->guests->guest(this->shownGuest))
      for (const Entry &e : this->entries)
        if (e.panel && e.id == 9 && e.open)
          if (Animation *a = this->guests->art(*g, "stand"))
            a->drawAnchored(renderer, static_cast<float>(e.rect.x + 34), static_cast<float>(e.rect.y + 62),
                            CompassDirection::SE, nullptr, 0);
  if (!this->animals || !this->isPanelOpen(6))
    return;
  const Animals::Member *m = this->animals->member(this->shownAnimal);
  if (!m)
    return;
  // What it's doing now (zoo.exe 0x4ee2ba: the unit's animation, slot 0x90),
  // its frame's box centred in the round window (0x4fab40 with -30005: the
  // middle; a land animal standing comes out with its feet at +34, +62 as
  // measured) - a porpoise's swim too, not a land animal's stand
  for (const Entry &e : this->entries)
    if (e.panel && e.id == 6 && e.open) {
      Animation *a = m->boxed ? this->animals->crate(*m) : this->animals->art(*m, m->anim.empty() ? "stand" : m->anim);
      if (!a && !m->boxed)
        a = this->animals->art(*m, "stand");
      if (!a)
        continue;
      const float cx = static_cast<float>(e.rect.x + 34), cy = static_cast<float>(e.rect.y + 37);
      // (a tank animal at the surface: its art is the part above the water,
      // its "under" layer the rest - both, the whole animal)
      Animation *under = m->boxed ? nullptr : this->animals->underArt(*m, a);
      float l, t, r, b;
      if (!a->anchoredBounds(0, 0, CompassDirection::SE, l, t, r, b))
        continue;
      // (inside its round window, the Trackanim button's art, as the
      // original draws it into the button: kept to the round window - a big
      // animal, a whale shark, filled the button's square past its rim)
      SDL_Rect clip{};
      if (UiElement *track = e.layout->getElementById(3114))
        clip = track->getLastRect();
      float ul, ut, ur, ub;
      const bool hasUnder = under && under->anchoredBounds(0, 0, CompassDirection::SE, ul, ut, ur, ub);
      if (hasUnder)
        l = std::min(l, ul), t = std::min(t, ut), r = std::max(r, ur), b = std::max(b, ub);
      auto drawIt = [&](float ox, float oy) {
        float x = cx - (l + r) * 0.5f + ox, y = cy - (t + b) * 0.5f + oy;
        if (hasUnder)
          under->drawAnchored(renderer, x, y, CompassDirection::SE, nullptr, 0);
        a->drawAnchored(renderer, x, y, CompassDirection::SE, nullptr, 0);
      };
      if (clip.w > 0 && clip.h > 0)
        drawRound(renderer, clip, drawIt);
      else
        drawIt(0, 0);
    }
}

// What draw(ox, oy) draws - offset by ox, oy - kept to the circle in box (a
// round window): drawn into a clear layer the box's size, everything
// outside the circle wiped, the layer put in place
void UiGameScreen::drawRound(SDL_Renderer *renderer, const SDL_Rect &box,
                             const std::function<void(float, float)> &draw) {
  static SDL_Texture *layer = nullptr, *mask = nullptr;
  static int lw = 0, lh = 0;
  if (!layer || lw != box.w || lh != box.h) {
    if (layer)
      SDL_DestroyTexture(layer);
    if (mask)
      SDL_DestroyTexture(mask);
    lw = box.w;
    lh = box.h;
    layer = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_TARGET, lw, lh);
    mask = nullptr;
    // (the circle: white, opaque inside, clear outside, a pixel's soft edge)
    if (SDL_Surface *s = SDL_CreateSurface(lw, lh, SDL_PIXELFORMAT_ARGB8888)) {
      const float rx = lw * 0.5f - 2.0f, ry = lh * 0.5f - 2.0f, mx = lw * 0.5f, my = lh * 0.5f;
      for (int y = 0; y < lh; y++)
        for (int x = 0; x < lw; x++) {
          float dx = (x + 0.5f - mx) / rx, dy = (y + 0.5f - my) / ry;
          float edge = (1.0f - std::sqrt(dx * dx + dy * dy)) * std::min(rx, ry);
          Uint8 av = static_cast<Uint8>(std::clamp(edge + 0.5f, 0.0f, 1.0f) * 255.0f);
          static_cast<Uint32 *>(s->pixels)[y * (s->pitch / 4) + x] = (static_cast<Uint32>(av) << 24) | 0xFFFFFF;
        }
      mask = SDL_CreateTextureFromSurface(renderer, s);
      SDL_DestroySurface(s);
    }
    if (layer)
      // (drawn over clear black, its colours already carry their alpha)
      SDL_SetTextureBlendMode(layer, SDL_ComposeCustomBlendMode(
                                         SDL_BLENDFACTOR_ONE, SDL_BLENDFACTOR_ONE_MINUS_SRC_ALPHA, SDL_BLENDOPERATION_ADD,
                                         SDL_BLENDFACTOR_ONE, SDL_BLENDFACTOR_ONE_MINUS_SRC_ALPHA, SDL_BLENDOPERATION_ADD));
    if (mask)
      // (what's under it kept as much as the circle's opaque there)
      SDL_SetTextureBlendMode(mask, SDL_ComposeCustomBlendMode(
                                        SDL_BLENDFACTOR_ZERO, SDL_BLENDFACTOR_SRC_ALPHA, SDL_BLENDOPERATION_ADD,
                                        SDL_BLENDFACTOR_ZERO, SDL_BLENDFACTOR_SRC_ALPHA, SDL_BLENDOPERATION_ADD));
  }
  if (!layer || !mask) {
    draw(0, 0);
    return;
  }
  // (what's set now, to put back after)
  SDL_Texture *was = SDL_GetRenderTarget(renderer);
  float sx = 1.0f, sy = 1.0f;
  SDL_GetRenderScale(renderer, &sx, &sy);
  SDL_Rect view, clip;
  SDL_GetRenderViewport(renderer, &view);
  const bool clipped = SDL_RenderClipEnabled(renderer);
  SDL_GetRenderClipRect(renderer, &clip);
  SDL_BlendMode mode;
  SDL_GetRenderDrawBlendMode(renderer, &mode);
  SDL_SetRenderTarget(renderer, layer);
  SDL_SetRenderScale(renderer, 1.0f, 1.0f);
  SDL_SetRenderViewport(renderer, nullptr);
  SDL_SetRenderClipRect(renderer, nullptr);
  SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
  SDL_SetRenderDrawColor(renderer, 0, 0, 0, 0);
  SDL_RenderClear(renderer);
  SDL_SetRenderDrawBlendMode(renderer, mode);
  draw(static_cast<float>(-box.x), static_cast<float>(-box.y));
  ZT_RenderCopy(renderer, mask, nullptr, nullptr);
  SDL_SetRenderTarget(renderer, was);
  SDL_SetRenderScale(renderer, sx, sy);
  SDL_SetRenderViewport(renderer, &view);
  SDL_SetRenderClipRect(renderer, clipped ? &clip : nullptr);
  ZT_RenderCopy(renderer, layer, nullptr, &box);
}

void UiGameScreen::showExhibit(int id) {
  if (this->fences) if (const auto *e=this->fences->exhibit(id)) if (e->show) { this->showShow(id); return; }
  this->showPanel(30, "");
  this->refreshExhibits();
  UiListBox *list = this->exhibitPanel
                        ? dynamic_cast<UiListBox *>(this->exhibitPanel->getElementById(4305))
                        : nullptr;
  if (!list)
    return;
  for (size_t i = 0; i < this->listedExhibits.size(); i++)
    if (this->listedExhibits[i] == id) {
      list->setSelectedIndex((int)i);
      // Clicked on the map: it's already in view
      auto centre = this->centreOn;
      this->centreOn = nullptr;
      this->refreshExhibits();
      this->centreOn = centre;
    }
}

void UiGameScreen::setFences(Fences *fences, std::function<void(float, float)> centreOn) {
  this->fences = fences;
  this->centreOn = centreOn;
  this->shownListCount = -1;
  this->shownExhibit = -1;
  this->tankPageShown = false;
}

UiElement *UiGameScreen::exhibitElement(int id) const {
  return this->exhibitPanel ? this->exhibitPanel->getElementById(id) : nullptr;
}

void UiGameScreen::syncExhibitTabs() {
  if (this->exhibitPanel)
    this->exhibitPanel->syncTabs();
}

void UiGameScreen::refreshExhibits() {
  UiLayout *z = this->exhibitPanel;
  if (!z || !this->fences)
    return;
  UiListBox *list = dynamic_cast<UiListBox *>(z->getElementById(4305));
  if (!list)
    return;
  // The list: every named exhibit
  std::vector<const Fences::Exhibit *> shown;
  for (const Fences::Exhibit &ex : this->fences->exhibits())
    if (ex.named)
      shown.push_back(&ex);
  // A tank's mini icon: its water clean (green), dirty (yellow, under
  // murkyWaterPurity 60) or very dirty (red, under 20); another exhibit's:
  // its fence (zoo.exe 0x5b3149: fences/lsgfen green, lsyfen worn, lsrfen
  // broken)
  auto iconOf = [this](const Fences::Exhibit *ex) -> std::string {
    if (!ex->tank) {
      static const char *fence[3] = {"fences/lsgfen/lsgfen", "fences/lsyfen/lsyfen", "fences/lsrfen/lsrfen"};
      return fence[std::clamp(this->fences->condition(ex->id), 0, 2)];
    }
    return ex->purity < 20   ? "ui/tanks/exbdirt2/exbdirt2"
           : ex->purity < 60 ? "ui/tanks/exbdirt1/exbdirt1"
                             : "ui/tanks/exbtank/exbtank";
  };
  bool relist = (int)shown.size() != this->shownListCount;
  for (size_t i = 0; !relist && i < shown.size(); i++)
    relist = this->listedExhibits[i] != shown[i]->id || this->listedIcons[i] != iconOf(shown[i]);
  if (relist) {
    int keep = this->shownExhibit;
    list->clear();
    this->listedExhibits.clear();
    this->listedIcons.clear();
    for (const Fences::Exhibit *ex : shown) {
      list->addItem(ex->name, "", iconOf(ex));
      this->listedExhibits.push_back(ex->id);
      this->listedIcons.push_back(iconOf(ex));
    }
    this->shownListCount = (int)shown.size();
    this->shownExhibit = -1;
    for (size_t i = 0; i < this->listedExhibits.size(); i++)
      if (this->listedExhibits[i] == keep) {
        list->setSelectedIndex((int)i);
        this->shownExhibit = keep;
      }
  }
  int sel = list->getSelectedIndex();
  int id = sel >= 0 && sel < (int)this->listedExhibits.size() ? this->listedExhibits[sel] : -1;
  const Fences::Exhibit *ex = id >= 0 ? this->fences->exhibit(id) : nullptr;
  auto element = [&](int eid) { return z->getElementById(eid); };
  auto text = [&](int eid, const std::string &s) {
    if (UiText *t = dynamic_cast<UiText *>(element(eid)))
      if (!t->isBeingEdited())
        t->setText(s);
  };
  const bool picked = id != this->shownExhibit;
  if (picked) {
    this->shownExhibit = id;
    // (zoo.exe mulhab_HabitatList_click 0x599ae7: the view to its gate's
    // tile, habitat +0x8c, through 0x59b90c - none, it stays)
    float cx = 0, cy = 0;
    if (ex && this->centreOn && this->exhibitGateTile(ex->id, cx, cy))
      this->centreOn(cx, cy);
    // (and its gate selected: 0x492ca3 - the habitat's gate piece, a fence
    // - to ui_main_4b2322(gate, 1), the selection arrow over it; measured
    // 2026-11-17, #266. That selection replaces the last: an animal's,
    // guest's or staff member's information closed - 0x4ee5a3, 0x4b2afd,
    // 0x58b750, the helicopter's 0x50f5fc - read in its code. A keeper's
    // pick of the exhibit doesn't select it: infostf2_AssignList_click
    // 0x61861b only picks it, 0x58b5ce)
    this->gateExhibit = -1;
    if (ex && !this->keeperPicking) {
      bool gate = false;
      if (this->fences)
        for (const auto &[e, p] : this->fences->pieces())
          if (p.gate && p.tank < 0 && this->fences->exhibitOf(e) == ex->id) {
            gate = true;
            break;
          }
      if (gate) {
        this->gateExhibit = ex->id;
        for (int info : {6, 9, 17, 1400})
          if (this->isPanelOpen(info))
            this->setPanelOpen(info, false);
      }
    }
  }
  // The page (zoo.exe 0x58b5ce, picked or the list opened): the page shown
  // stays - only the first tank picked since the zoo loaded goes to Modify
  // Tank (0x468b9d, once: DAT_0063ead5); anything else leaves Modify Tank
  // for Status, and the tank tab is greyed (0x58b5f2). Then
  // exhibitListRefresh 0x5b108b: one picked, Animals, Thoughts or
  // Popularity (701-703) showing stays, else Status (700); none, Status
  // hidden and the others left as they are, emptied. measured 2026-11-18
  // (Play original): a first tank picked on Status -> Modify Tank; Animals
  // kept from tank to tank, Thoughts from land to land and to a tank;
  // Modify Tank kept from tank to tank (Status not shown under it), left
  // for Status at a land exhibit; reopened on Thoughts, nothing showing
  if (picked || this->exhibitPagesDue) {
    this->exhibitPagesDue = false;
    UiButton *tankTab = dynamic_cast<UiButton *>(element(4365));
    UiButton *statusTab = dynamic_cast<UiButton *>(element(4310));
    if (tankTab)
      tankTab->setDisabled(!ex || !ex->tank);
    if (ex && ex->tank) {
      if (!this->tankPageShown && tankTab)
        tankTab->choose();
      this->tankPageShown = true;
    } else if (tankTab && tankTab->isToggledOn() && statusTab) {
      statusTab->choose();
    }
    if (ex && statusTab) {
      bool page = false;
      for (int tab : {4311, 4312, 4313, 4365})
        if (UiButton *b = dynamic_cast<UiButton *>(element(tab)))
          page = page || b->isToggledOn();
      if (!page)
        statusTab->choose();
    }
    z->syncTabs();
  }
  // Nothing picked: Status hidden (0x5b108b), the lists and Popularity
  // emptied (0x467fc8(0), 0x467e76(0), 0x467c6f: no stars, $0.00)
  if (!ex) {
    for (int sub : {700, 704})
      if (UiElement *l = element(sub))
        l->setHidden(true);
    if (!this->listedExhibitAnimals.empty())
      if (UiListBox *l = dynamic_cast<UiListBox *>(element(4326)))
        l->clear();
    if (!this->listedExhibitThoughts.empty())
      if (UiListBox *l = dynamic_cast<UiListBox *>(element(4330)))
        l->clear();
    this->listedExhibitAnimals.clear();
    this->listedExhibitThoughts.clear();
    this->litExhibitAnimal = -1;
    for (int eid : {4319, 4317, 4321})
      text(eid, formatCents(0));
    if (UiImage *stars = dynamic_cast<UiImage *>(element(4315))) {
      std::vector<std::string> set = stars->getImageSet();
      if (!set.empty() && stars->getImagePath() != set[0])
        stars->setImage(set[0]);
    }
  }
  if (UiElement *name = element(4304))
    name->setHidden(!ex);
  if (!ex)
    return;
  // Its Status page (zoo.exe mulhab_StatusTab1_click 0x4681d0): popularity,
  // the books (habitat +0xfc.. donations, +0x108.. upkeep), when it was
  // built (4349, none for a habitat with +0x2c set), staff assigned. (Its
  // 4353-4357, +0x114..+0x11c, are in no layout.) The tabs only switch the
  // page and refresh it: Status 0x4699d2, Animals 0x469af2 (0x467fc8),
  // Thoughts 0x46981e (0x467e76), Modify Tank 0x468c00 (0x5ae509)
  text(4304, ex->name);
  text(4349, this->resource_manager->getString(22101 + ex->constructedMonth) + " " +
                 std::to_string(ex->constructedDay) + ", Year " +
                 std::to_string(ex->constructedYear));
  // Upkeep and donations: last month, this month, all told
  auto cents = [](double v) { return formatCents(std::lround(v * 100)); };
  text(4325, cents(ex->upkeepLast));
  text(4323, cents(ex->upkeepNow));
  text(4347, cents(ex->upkeepTotal));
  text(4319, cents(ex->donationsLast));
  text(4317, cents(ex->donationsNow));
  text(4321, cents(ex->donationsTotal));
  // Staff Assigned: a keeper given this exhibit
  bool assigned = false;
  if (this->staff)
    for (const Staff::Member &m : this->staff->members())
      for (int e : m.exhibits)
        assigned = assigned || e == id;
  text(4364, this->resource_manager->getString(assigned ? 4360 : 4361));
  // Animals (zoo.exe 0x467fc8): each one in it by name, its mini icon and
  // name, the one selected in the zoo lit; picking one selects it (its
  // information) and centres the view on it
  if (UiListBox *animalList = dynamic_cast<UiListBox *>(element(4326))) {
    std::vector<int> in;
    if (this->animals)
      for (const Animals::Member &m : this->animals->members())
        if (m.exhibit == id && !m.escaped && !m.boxed)
          in.push_back(m.id);
    std::stable_sort(in.begin(), in.end(), [this](int a, int b) {
      return this->animals->member(a)->name < this->animals->member(b)->name;
    });
    int lit = -1;
    for (size_t i = 0; i < in.size(); i++)
      if (this->animals && in[i] == this->animals->selected)
        lit = static_cast<int>(i);
    if (in != this->listedExhibitAnimals) {
      animalList->clear();
      for (int aid : in) {
        const Animals::Member *m = this->animals->member(aid);
        animalList->addItem(m->name, "", this->animals->types()[m->type].listImage);
      }
      this->listedExhibitAnimals = in;
      this->litExhibitAnimal = lit;
      animalList->setSelectedIndex(lit);
    }
    // (a pick: the bulldozer put away, the animal selected and the view
    // centred on it, its information shown - zoo.exe
    // mulhab2_AnimalList_click 0x610755: bulldozeToolOff 0x443b60,
    // ui_main_4b2322(it, 1), centreViewOn 0x59b90c at its tile)
    int pick = animalList->getSelectedIndex();
    if (pick != this->litExhibitAnimal && pick >= 0 && pick < (int)in.size()) {
      int aid = in[pick];
      this->litExhibitAnimal = pick;
      this->bulldozerOff();
      this->showAnimal(aid);
      if (this->animalRequest)
        this->animalRequest(AnimalRequest::Select, aid);
    } else if (lit != this->litExhibitAnimal) {
      this->litExhibitAnimal = lit;
      animalList->setSelectedIndex(lit);
    }
  }
  // Thoughts: what guests have thought about it, newest first
  if (UiListBox *thoughtList = dynamic_cast<UiListBox *>(element(4330))) {
    std::vector<std::string> said;
    if (this->guests)
      said = this->guests->thoughtsAbout(-1, id, 20);
    if (said != this->listedExhibitThoughts) {
      // (wrapped, in the list's forecolor: mulhab3.lyt 156,205,183)
      thoughtList->setWrapped(true);
      thoughtList->clear();
      for (const std::string &s : said)
        thoughtList->addItem(s, SDL_Color{156, 205, 183, 255});
      this->listedExhibitThoughts = said;
    }
  }
  // Popularity: a half star each 10 (0star ... 5star: (11 - 1) x pop / 100)
  if (UiImage *stars = dynamic_cast<UiImage *>(element(4315))) {
    std::vector<std::string> set = stars->getImageSet();
    if (!set.empty()) {
      int pop = ex->popularity(this->guests ? this->guests->clock : 0);
      size_t frame = std::min(set.size() - 1, static_cast<size_t>((set.size() - 1) * pop / 100));
      if (stars->getImagePath() != set[frame])
        stars->setImage(set[frame]);
    }
  }
  // Tank Adjustment (zoo.exe tankmod_Buttons_click 0x469128, its refresh
  // 0x5ae509): the wall and base arrows (greyed where they can go no
  // further) and a step's price; filled, the water's salinity and Drain;
  // drained, Fill Tank with salt or fresh at their prices
  bool tank = ex->tank || ex->groundHeight != ex->floorHeight;
  bool full = tank && ex->tank && ex->filling;
  auto show = [&](int eid, bool on) {
    if (UiElement *e = element(eid))
      e->setHidden(!on);
  };
  auto grey = [&](int eid, bool off) {
    if (UiElement *e = element(eid))
      e->setDisabled(off);
  };
  for (int eid : {4874, 4875, 4879, 4880, 4877, 4882})
    show(eid, tank);
  grey(4874, !this->fences->canAdjustWall(id, -1));
  grey(4875, !this->fences->canAdjustWall(id, +1));
  grey(4879, !this->fences->canAdjustBase(id, -1));
  grey(4880, !this->fences->canAdjustBase(id, +1));
  // (the amounts alone, as the original shows them, cut to dollars:
  // zoo.exe 0x5ae509 - 4877 the walls a step up from where they are,
  // 0x4ef687, $0 under initialHeight; 4882 a base step, habitat +0x1a4.
  // Its 4887, the water's price, is in no layout)
  text(4877, formatCost(static_cast<int>(this->fences->wallChangeCost(id, ex->wallSub, ex->wallSub + 1))));
  text(4882, formatCost(this->fences->baseStepCost(id)));
  for (int eid : {4867, 4868, 4883, 4885})
    show(eid, full);
  for (int eid : {4881, 4869, 4870, 4891, 4893, 4871, 4872})
    show(eid, tank && !full);
  grey(4885, false);
  // (Salt / Fresh only once the water's all gone: habitat +0x188, the
  // water in height units, under 1 - zoo.exe 0x5ae509)
  const bool wet = ex->water * static_cast<float>(ex->wallSub - 1) >= 1.0f;
  grey(4869, wet);
  grey(4870, wet);
  // (Salinity_Type, 0x1304: zoo.exe 0x468fe4 sets its text)
  text(4868, this->resource_manager->getString(ex->salt ? 4888 : 4889));
  text(4871, formatCost(this->fences->fillCost(id, true)));
  text(4872, formatCost(this->fences->fillCost(id, false)));
}
