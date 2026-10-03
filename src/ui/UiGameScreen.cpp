#include "UiGameScreen.hpp"

#include <algorithm>
#include <functional>
#include <set>

#include "../ItemCatalog.hpp"
#include "../Utils.hpp"
#include "UiButton.hpp"
#include "UiLayout.hpp"
#include "UiScrollingRegion.hpp"
#include "UiImage.hpp"
#include "UiText.hpp"

UiGameScreen::UiGameScreen(ResourceManager *resource_manager,
                           const std::string &mainLayout,
                           const std::string &screenLayout) {
  this->resource_manager = resource_manager;

  IniReader *mainReader = resource_manager->getIniReader(mainLayout);
  if (!mainReader) {
    SDL_Log("[GameScreen] %s not found", mainLayout.c_str());
    return;
  }
  int mainLayer = mainReader->getInt("layoutinfo", "layer", 2);
  this->hud = new UiLayout(mainReader, resource_manager);
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

  // The panels are the screen layout's sub-layouts with those ids
  IniReader *screen = resource_manager->getIniReader(screenLayout);
  if (screen) {
    std::string mainLower = Utils::string_to_lower(mainLayout);
    for (const std::string &section : screen->getSections()) {
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
    delete screen;
  }

  // Drawn by layer; the screen file's order breaks ties
  std::stable_sort(this->entries.begin(), this->entries.end(),
                   [](const Entry &a, const Entry &b) { return a.layer < b.layer; });

  this->setupBuyPanels();
}

static std::string formatPrice(int amount) {
  std::string digits = std::to_string(amount);
  for (int i = static_cast<int>(digits.size()) - 3; i > 0; i -= 3)
    digits.insert(i, ",");
  return "$" + digits;
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
                      {10, 0, 3620, 3611, 3613, "staff"}}; // personel.lyt
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
    p.region->onSelect = [this, self](int index) { this->showItem(*self, index); };
    this->refreshBuyPanel(p);
  }
}

// The chosen tab's category fills the grid (the first item picked)
void UiGameScreen::refreshBuyPanel(BuyPanel &p) {
  std::string category = p.fixedCategory;
  for (UiButton *tab : p.tabs)
    if (tab->isToggledOn())
      category = tab->getStringData();
  if (category.empty() || category == p.category)
    return;
  p.category = category;
  // The panel's title is the chosen tab's name ("Creatures", "Buildings")
  for (UiButton *tab : p.tabs)
    if (tab->isToggledOn() && p.title)
      p.title->setText(this->resource_manager->getString(tab->getHelpId()));
  p.items = ItemCatalog::get().inCategory(category);
  std::vector<UiScrollingRegion::Item> cells;
  for (const CatalogItem *item : p.items)
    cells.push_back({item->icon, item->file});
  p.region->setItems(cells);
  p.region->setSelected(p.items.empty() ? -1 : 0);
  this->showItem(p, p.items.empty() ? -1 : 0);
}

void UiGameScreen::showItem(BuyPanel &p, int index) {
  const CatalogItem *item =
      index >= 0 && index < (int)p.items.size() ? p.items[index] : nullptr;
  if (p.name)
    p.name->setText(item ? item->name : "");
  if (p.cost)
    p.cost->setText(item && item->cost ? formatPrice(item->cost) : "");
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
  show(p.rotateLeft, rotates);
  show(p.rotateRight, rotates);
  show(p.era, false);

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
  show(p.fenceLabel, fence);
  show(p.fenceType, fence && item);
  if (fence && item)
    if (UiText *t = dynamic_cast<UiText *>(element(p.fenceType)))
      t->setText(this->resource_manager->getString(
          item->showFence ? 3299
          : item->members.count("habitatfences") ? 3298
                                                  : 3297));
  show(p.gate, fence);
  if (UiElement *gate = element(p.gate))
    gate->setDisabled(true);
}

UiGameScreen::~UiGameScreen() {
  for (Entry &e : this->entries)
    delete e.layout;
}

UiElement *UiGameScreen::getElementById(int id) {
  for (Entry &e : this->entries)
    if (UiElement *found = e.layout->getElementById(id))
      return found;
  return nullptr;
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
    for (UiButton *tab : p.tabs)
      tab->setToggledOn(Utils::string_to_lower(tab->getStringData()) == category);
    this->refreshBuyPanel(p);
  }
}

void UiGameScreen::setPanelOpen(int id, bool open) {
  for (Entry &e : this->entries)
    if (e.panel && e.id == id)
      e.open = open;
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
  return e.layout->computeRect(renderer, &parent);
}

void UiGameScreen::draw(SDL_Renderer *renderer, SDL_Rect *layout_rect) {
  for (Entry &e : this->entries) {
    if (!e.open)
      continue;
    if (!e.panel) {
      e.layout->draw(renderer, layout_rect);
      continue;
    }
    SDL_Rect r = this->panelRect(renderer, e, layout_rect);
    e.layout->setAnchorRect(this->anchorRect(e, layout_rect));
    e.layout->draw(renderer, &r);
  }
}

UiAction UiGameScreen::handleInputs(std::vector<Input> &inputs) {
  UiAction action = UiAction::NONE;
  // Top-most first
  for (auto it = this->entries.rbegin(); it != this->entries.rend(); ++it) {
    if (!it->open)
      continue;
    UiAction a = it->layout->handleInputs(inputs);
    // "Close the panel I'm in"
    if (isPanelClose(a) && panelOf(a) == kPanelSelf)
      a = (UiAction)(kPanelCloseAction + it->id);
    if (a != UiAction::NONE && action == UiAction::NONE)
      action = a;
  }

  // A tab may have changed what a buy panel lists
  for (BuyPanel &p : this->buyPanels)
    this->refreshBuyPanel(p);

  if (isPanelToggle(action)) {
    // The panel buttons are toggles in one radio set: each open panel is
    // the one whose button is on
    for (UiButton *b : this->panelButtons)
      this->setPanelOpen(b->getActionTarget(), b->isToggledOn());
  } else if (isPanelClose(action)) {
    int id = panelOf(action);
    this->setPanelOpen(id, false);
    for (UiButton *b : this->panelButtons)
      if (b->getActionTarget() == id)
        b->setToggledOn(false);
  }
  return action;
}
