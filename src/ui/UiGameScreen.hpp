#ifndef UI_GAME_SCREEN_HPP
#define UI_GAME_SCREEN_HPP

#include <map>
#include <string>
#include <vector>

#include "UiElement.hpp"

class UiButton;
class UiLayout;
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

  bool isPanelOpen(int id) const;
  void setPanelOpen(int id, bool open);
  // Opens a panel as its HUD button would (closing the others) and, for a
  // buy panel, picks the tab for a category (empty: leave the tabs)
  void showPanel(int id, const std::string &category);

private:
  struct Entry {
    UiLayout *layout = nullptr;
    int id = 0;
    int layer = 0;
    bool panel = false;
    bool open = false;
  };
  std::vector<Entry> entries; // in drawing order
  UiLayout *hud = nullptr;
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
  // Which details the chosen tab shows, as the original does
  void showDetails(BuyPanel &panel, const CatalogItem *item);
  std::map<int, std::string> infoImages; // ui/infoimg.cfg: id -> image
};

#endif // UI_GAME_SCREEN_HPP
