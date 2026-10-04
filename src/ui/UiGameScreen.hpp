#ifndef UI_GAME_SCREEN_HPP
#define UI_GAME_SCREEN_HPP

#include <functional>
#include <map>
#include <string>
#include <vector>

#include "UiElement.hpp"
#include "../Staff.hpp"

class UiButton;
class UiImage;
class UiLayout;
class UiListBox;
class ZooSim;
class Fences;
class UiScrollingRegion;
class UiText;
struct CatalogItem;

// ============================================================================
// THE IN-GAME SCREEN (ui/gamescrn.lyt)
// ============================================================================
// The original's in-game screen is a stack of layouts: the HUD (main.lyt)
// and its panels (buya.lyt, buyobj.lyt, ...), each hidden until opened and
// drawn in order of their [LayoutInfo] layer. The buy panels are layer 1
// and the HUD layer 2, so the panels slide out from under the toolbar.
//
// A HUD button with action=3 target=<layout id> toggles that panel; the
// toolbar buttons are a radio set, so opening one closes the others. A
// panel's close button (action=2 target=<its id>) closes it again.
// ============================================================================
class UiGameScreen : public UiElement {
public:
  UiGameScreen(ResourceManager *resource_manager, const std::string &mainLayout,
               const std::string &screenLayout);
  ~UiGameScreen();

  UiAction handleInputs(std::vector<Input> &inputs) override;
  void draw(SDL_Renderer *renderer, SDL_Rect *layout_rect) override;

  // Searches the HUD, then the panels
  UiElement *getElementById(int id);

  // The Zoo Status panel (zooinfo.lyt): what it shows about the zoo
  struct ZooInfo {
    std::string name;        // the entrance's
    double admission = 22.0; // adult; children pay half
    long cash = 0;
    long placedValue = 0;    // purchase cost of everything on the map
    int rating = 0;          // zoo rating, 0-100
    std::string month;       // the current month ("Jan")
    int staffCount = 0;
  };
  void setZooInfo(const ZooInfo &info);
  // The game clock and books the panels show and charge funding to
  void setSim(ZooSim *sim);

  // Whether a point (layout units) is on the HUD, a panel or a dialog (the
  // map doesn't get the mouse there)
  bool isOverHud(int x, int y) const;
  // The fence picked in Buy Habitat while it is open (its .ai file; empty:
  // none)
  std::string fenceToolFile() const;
  // The staff picked in Hire Staff while it is open (its .ai file)
  std::string staffToolFile() const;
  // The path picked on Buy Habitat's Paths tab while it is open
  std::string pathToolFile() const;
  // Staff: their panels (Staff Information, ui/infostf.lyt; Staff List,
  // ui/mulstaff.lyt) and what their buttons ask of the world
  enum class StaffRequest { PickUp, Fire, Assign, Track, Select };
  void setStaff(Staff *staff, std::function<void(StaffRequest, int)> request);
  void showStaff(int id);
  int shownStaffId() const { return this->shownStaff; }
  void refreshStaff();
  // The bulldozer is on (the HUD's Clear Objects button)
  bool bulldozerOn() const;

  // Modal dialogs: ui/newname.lyt (a name to type, OK) and ui/confirm.lyt
  // (a question, Yes / No)
  void askName(const std::string &prompt, const std::string &initial,
               std::function<void(const std::string &)> done);
  void askConfirm(const std::string &message, std::function<void()> yes);
  bool hasDialog() const { return this->dialog.layout != nullptr; }
  void drawDialog(SDL_Renderer *renderer, SDL_Rect *layout_rect);

  // The exhibits (Exhibit/Show List, ui/mulhab.lyt)
  void setFences(Fences *fences, std::function<void(float, float)> centreOn);
  // The buy panels list again (a new month unlocked things), keeping what
  // is picked
  void refreshCatalog();
  // Opens the Exhibit/Show List on an exhibit (clicked on the map)
  void showExhibit(int id);
  // Opens a tank filter's panel (filter.lyt: its name, Sell, its status
  // and upkeep)
  void showFilter(int index);
  int shownFilterIndex() const { return this->shownFilter; }
  // The message bar at the top: a warning for a few seconds (red, as the
  // original's "Zoo objects can only be placed inside the main zoo
  // wall."), and "The game is paused." while it is
  void showMessage(const std::string &text, SDL_Color color = {255, 40, 40, 255},
                   Uint32 ms = 4000);
  void setPausedMessage(bool paused) { this->pausedMessage = paused; }
  // Tooltips, as the original: resting the cursor on a button shows its
  // help (lang string 30000 + its helpid: "Adjust wall up. Some animals
  // prefer a deep tank."); over the map, the tool's hint set here ("Click
  // to place a single fence piece, ...")
  void setWorldTip(const std::string &tip) { this->worldTip = tip; }
  // The tooltip that would show now (for tests): its text
  std::string tooltipText() const;
  void setMouse(int x, int y, Uint32 since) {
    this->mouseX = x;
    this->mouseY = y;
    this->mouseStill = since;
  }
  // A buy panel's grid scrolled to a row (for comparing pages with the
  // original); its last row it can scroll to
  int scrollBuyPanel(int panelId, int row);
  void refreshExhibits();
  const ZooInfo &zooInfo() const { return this->zoo; }

  bool isPanelOpen(int id) const;
  // Whether a point (layout units) is on an open panel (the wheel scrolls
  // its lists there rather than zooming the map)
  bool isOverPanel(int x, int y) const;
  void setPanelOpen(int id, bool open);
  // Opens a panel as its HUD button would (closing the others) and, for a
  // buy panel, picks the tab for a category (empty: leave the tabs)
  void showPanel(int id, const std::string &category);
  // ESC: opens the game menu (Game Options), or closes it (or a drop-down
  // list) when open
  void toggleGameMenu();

  // Picks a panel's tab by its button id (e.g. the Terrain Types tab)
  void showTab(int panelId, int buttonId);
  // The content filter of a panel: drop its list down, or pick a choice
  // (0 all, 1 Zoo Tycoon, 2 Dinosaur Digs, 3 Marine Mania)
  void openFilter(int panelId);
  // The filter list, when it is down: drawn after everything else (its own
  // art and text passes, so no panel text shows through it)
  bool hasPopup() const { return this->filter.open; }
  void drawPopup(SDL_Renderer *renderer, SDL_Rect *layout_rect);
  void setFilter(int panelId, int choice);

private:
  struct Entry {
    UiLayout *layout = nullptr;
    int id = 0;
    int layer = 0;
    bool panel = false;
    bool open = false;
    SDL_Rect rect = {0, 0, 0, 0}; // where it was last drawn
  };
  std::vector<Entry> entries; // in drawing order
  UiLayout *hud = nullptr;
  UiImage *messageBar = nullptr;
  std::string message;
  SDL_Color messageColor = {255, 255, 255, 255};
  Uint32 messageUntil = 0;
  bool pausedMessage = false;
  void drawMessage(SDL_Renderer *renderer, SDL_Rect *layout_rect);
  int mouseX = -1, mouseY = -1;
  Uint32 mouseStill = 0;
  std::string worldTip;
  void drawTooltip(SDL_Renderer *renderer, SDL_Rect *layout_rect);
  std::vector<UiButton *> panelButtons; // HUD buttons with action=3

  SDL_Rect panelRect(SDL_Renderer *renderer, const Entry &e,
                     SDL_Rect *screen);
  SDL_Rect anchorRect(const Entry &e, SDL_Rect *screen);

  // The buy panels (animals, objects, habitat, staff): their grid lists the
  // items of the chosen tab's category, and the picked item's name and
  // price show below it
  struct BuyPanel {
    int id = 0;
    UiScrollingRegion *region = nullptr;
    std::vector<UiButton *> tabs;
    std::string fixedCategory; // a panel without tabs (staff)
    UiText *title = nullptr;
    UiText *name = nullptr;
    UiText *cost = nullptr;
    UiText *duties = nullptr; // staff
    // The details under the grid (ids from the panel's layout; 0 = none)
    int male = 0, female = 0, info = 0, rotateLeft = 0, rotateRight = 0;
    int location = 0, habitat = 0, habitatFrame = 0, pref = 0, prefFrame = 0;
    int capacityLabel = 0, capacity = 0, era = 0;
    int fenceLabel = 0, fenceType = 0, gate = 0;
    UiLayout *layout = nullptr;
    std::string category;
    std::vector<const CatalogItem *> items;
  };
  std::vector<BuyPanel> buyPanels;
  void setupBuyPanels();
  void refreshBuyPanel(BuyPanel &panel);
  void showItem(BuyPanel &panel, int index);
  // The rotate buttons: every item in the grids turns a quarter, all
  // facing the same way (as in the original)
  int facing = 0; // which of the items' icons (0 SE, 1 SW, 2 NW, 3 NE)
  void rotateItems(int step);
  const std::string &facingIcon(const CatalogItem *item) const;
  // Which details the chosen tab shows, as the original does
  void showDetails(BuyPanel &panel, const CatalogItem *item);
  std::map<int, std::string> infoImages; // ui/infoimg.cfg: id -> image

  // The Buy Habitat panel's Terrain Types and Terrain Height tabs, which
  // show ui/teraform.lyt over the panel
  struct Terrain {
    int type = 0;
    std::string icon, name;
    int cost = 0; // per tile
  };
  struct TerraformPage {
    UiLayout *panel = nullptr; // buldhab.lyt
    UiButton *typesTab = nullptr, *heightTab = nullptr;
    UiScrollingRegion *region = nullptr;
    UiImage *brush = nullptr;
    std::vector<std::string> brushes; // brush1 (1x1) .. brush5 (5x5)
    UiButton *plus = nullptr, *minus = nullptr;
    int brushIndex = 4;
    std::vector<Terrain> terrains;
    int selected = 0;
    bool shown = false;
    std::map<int, bool> covered; // the panel's own elements' hidden state
  } terraform;
  void setupTerraform();
  void refreshTerraform();

  // The research panel (research.lyt): its Status page (resrch1.lyt) shows
  // what each branch is working on; the Research and Conservation tabs
  // share one page (resrch2.lyt) listing that branch's categories, its
  // funding, and the shown category's program
  struct ResearchPanel {
    UiLayout *panel = nullptr;
    UiButton *researchTab = nullptr, *conservationTab = nullptr;
    int shownBranch = -1; // the category page's branch
    int shownFilter = -1; // and the content filter it was listed with
    std::vector<int> listed; // list row -> category
  } research;

  // The content filter: a panel's "All" button (action=1 target=152) drops
  // down ui/xpac.lyt's list - All, Zoo Tycoon, Dinosaur Digs, Marine Mania -
  // and the panel lists only that pack's items (by cExpansionID; research
  // categories by the pack that adds them)
  struct FilterPopup {
    UiLayout *layout = nullptr;
    UiListBox *list = nullptr;
    bool open = false;
    int owner = 0; // the panel it dropped down from
  } filter;
  std::map<int, int> filterChoice; // panel id -> 0 all, 1..3 a pack

  // An action a button the game handles asked for (the game menu's Main
  // Menu and Exit Game), returned from the next handleInputs
  UiAction pendingAction = UiAction::NONE;
  // Whether an item of a pack (0..2) shows with a panel's filter
  bool passesFilter(int panelId, int expansion) const {
    auto it = this->filterChoice.find(panelId);
    return it == this->filterChoice.end() || it->second == 0 ||
           it->second - 1 == expansion;
  }
  void setupResearch();
  void refreshResearch();

  struct Dialog {
    UiLayout *layout = nullptr;
    int textId = 0;          // the editable box (newname)
    std::string typed;
    std::function<void(const std::string &)> done;
    std::function<void()> yes;
    bool submit = false, cancel = false;
  } dialog;
  std::vector<std::function<void()>> dialogQueue; // waiting their turn
  void closeDialog();
  void handleDialog(std::vector<Input> &inputs);

  Fences *fences = nullptr;
  std::function<void(float, float)> centreOn;
  UiLayout *exhibitPanel = nullptr;
  UiLayout *filterPanel = nullptr;
  Staff *staff = nullptr;
  std::function<void(StaffRequest, int)> staffRequest;
  UiLayout *staffPanel = nullptr, *staffList = nullptr;
  int shownStaff = -1, staffFilter = 0, staffListCount = -1;
  std::vector<int> listedStaff;
  bool tracking = false;
  void setupStaffPanels();
  void drawStaffPortrait(SDL_Renderer *renderer);
  int shownFilter = -1;
  void setupFilterPanel();
  void refreshFilterPanel();
  std::vector<int> listedExhibits;
  std::vector<std::string> listedIcons; // each one's mini icon
  int shownExhibit = -1, shownListCount = -1;
  void setupExhibits();

  // Zoo Status: its pages' texts, graphs, marketing and admission
  ZooInfo zoo;
  ZooSim *sim = nullptr;
  UiLayout *zooPanel = nullptr;
  void chargeFunding(); // research, conservation, marketing a day
  struct MarketingLevel {
    std::string name; // "%s none"
    int cost = 0;     // a month
  };
  std::vector<MarketingLevel> marketing;
  int marketingLevel = 0;
  void setupZooStatus();
  void refreshZooStatus();
};

#endif // UI_GAME_SCREEN_HPP
