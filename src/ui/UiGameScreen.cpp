#include "UiGameScreen.hpp"

#include <algorithm>
#include <functional>
#include <set>

#include "../ItemCatalog.hpp"
#include "../Research.hpp"
#include "../Fences.hpp"
#include "../ZooSim.hpp"
#include "../Utils.hpp"
#include "UiButton.hpp"
#include "UiGraph.hpp"
#include "UiLayout.hpp"
#include "UiScrollingRegion.hpp"
#include "UiImage.hpp"
#include "UiListBox.hpp"
#include "UiStatusImage.hpp"
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
  // Panels opened by clicking things on the map, not by a HUD button: a
  // tank filter's
  targets.insert(46);
  targets.insert(17); // a staff member's, clicked on the map or the list
  targets.insert(6);  // an animal's, clicked on the map
  targets.insert(28); // its Zookeeper Recommendations
  targets.insert(9);  // a guest's, clicked on the map
  targets.insert(21); // a stand's Building Information
  targets.insert(20); // another building's
  targets.insert(29); // the Message List (the Messages button, 1006)
  targets.insert(302); // a building's paint tab (infocomC.lyt)
  targets.insert(37);  // its colour picker (color.lyt)

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

  this->setupBuyPanels();
  this->setupTerraform();
  this->setupResearch();
  this->setupZooStatus();
  this->setupExhibits();
  this->setupFilterPanel();
  this->setupStaffPanels();
  this->setupLists();
  this->setupColours();
  this->setupAnimalPanel();

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
    this->filter.layout = new UiLayout(xpac, resource_manager);
    this->filter.list =
        dynamic_cast<UiListBox *>(this->filter.layout->getElementById(22900));
    if (this->filter.list)
      for (int id = 22900; id <= 22903; id++)
        this->filter.list->addItem(id);
  }
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
  if (!this->filter.list)
    return;
  this->filter.open = true;
  this->filter.owner = panelId;
  // The current choice is highlighted
  auto it = this->filterChoice.find(panelId);
  this->filter.list->setSelectedIndex(it == this->filterChoice.end() ? 0
                                                                     : it->second);
}

void UiGameScreen::setFilter(int panelId, int choice) {
  this->filter.open = false;
  this->filterChoice[panelId] = choice;
  for (Entry &e : this->entries) {
    if (!e.panel || e.id != panelId)
      continue;
    // The button shows the choice
    std::function<void(UiElement *)> label = [&](UiElement *el) {
      if (UiButton *b = dynamic_cast<UiButton *>(el))
        if (b->getActionType() == 1 && b->getActionTarget() == 152)
          b->setLabel(this->resource_manager->getString(22900 + choice));
      for (UiElement *c : el->getChildren())
        label(c);
    };
    label(e.layout);
  }
  for (BuyPanel &p : this->buyPanels)
    if (p.id == panelId) {
      p.category.clear(); // list again
      this->refreshBuyPanel(p);
    }
  if (panelId == 15)
    this->refreshResearch();
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
    p.region->onSelect = [this, self](int index) {
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
      // Picking a fence leaves gate mode
      if (self->id == 8 && index >= 0 && self->gate)
        if (UiButton *g = dynamic_cast<UiButton *>(self->layout->getElementById(self->gate)))
          g->setToggledOn(false);
      this->showItem(*self, index);
    };
    // Buy Habitat's gate button: a toggle (gate mode)
    if (p.id == 8 && p.gate)
      if (UiButton *g = dynamic_cast<UiButton *>(p.layout->getElementById(p.gate))) {
        g->setToggle(true);
        g->setToggledOn(false); // (off to start: toggles start on)
        g->onClick = [this, g] { this->setGateMode(g->isToggledOn()); };
      }
    // Adopt Animals: the male or female button takes the animal picked
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
          this->rotateItems(-1);
      };
    if (UiButton *right = dynamic_cast<UiButton *>(
            p.rotateRight ? p.layout->getElementById(p.rotateRight) : nullptr))
      right->onClick = [this, self] {
        if (self->category == "paths" && this->walkwayHeight)
          this->walkwayHeight(+1);
        else
          this->rotateItems(+1);
      };
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
  auto choice = this->filterChoice.find(p.id);
  for (UiButton *tab : p.tabs)
    if (tab->isToggledOn() && p.title)
      p.title->setText(this->resource_manager->getString(tab->getHelpId(
          choice == this->filterChoice.end() ? 0 : choice->second)));
  p.items.clear();
  for (const CatalogItem *item : ItemCatalog::get().inCategory(category))
    if (this->passesFilter(p.id, item->expansion))
      p.items.push_back(item);
  std::vector<UiScrollingRegion::Item> cells;
  for (const CatalogItem *item : p.items)
    cells.push_back({this->facingIcon(item), item->file});
  p.region->setItems(cells);
  p.region->setSelected(p.items.empty() ? -1 : 0);
  this->showItem(p, p.items.empty() ? -1 : 0);
  // "No items to display." (ZUPDATE's NoItemText) over an empty list
  for (int id : {2009, 3009, 3609})
    if (UiElement *none = p.layout ? p.layout->getElementById(id) : nullptr)
      none->setHidden(!p.items.empty());
}

// An item's icon for the current facing (items with fewer icons than
// facings, like the animals, keep theirs)
const std::string &UiGameScreen::facingIcon(const CatalogItem *item) const {
  if (item->icons.empty())
    return item->icon;
  return item->icons[this->facing % item->icons.size()];
}

bool UiGameScreen::selectBuyItem(int panel, const std::string &file) {
  for (BuyPanel &p : this->buyPanels) {
    if (p.id != panel || !p.region)
      continue;
    for (int i = 0; i < (int)p.items.size(); i++)
      if (p.items[i]->file == file) {
        p.region->setSelected(i);
        this->showItem(p, i);
        return true;
      }
  }
  return false;
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

void UiGameScreen::rotateItems(int step) {
  this->facing = ((this->facing + step) % 4 + 4) % 4;
  for (BuyPanel &p : this->buyPanels)
    for (int i = 0; i < (int)p.items.size(); i++)
      p.region->setItemIcon(i, this->facingIcon(p.items[i]));
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
  // Paths: the walkway height arrows, for the path types that can be raised
  const bool raises = c == "paths" && item && this->walkwayHeight && this->canRaise &&
                      this->canRaise(item->file);
  show(p.rotateLeft, rotates || raises);
  show(p.rotateRight, rotates || raises);
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

bool UiGameScreen::gateMode() const {
  if (!this->isPanelOpen(8))
    return false;
  for (const BuyPanel &p : this->buyPanels)
    if (p.id == 8 && p.category == "fence" && p.gate)
      if (UiButton *b = dynamic_cast<UiButton *>(p.layout->getElementById(p.gate)))
        return b->isToggledOn() && !b->isDisabled();
  return false;
}

void UiGameScreen::setGateMode(bool on) {
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
  r.researchTab = dynamic_cast<UiButton *>(r.panel->getElementById(4009));
  r.conservationTab = dynamic_cast<UiButton *>(r.panel->getElementById(4010));

  // The content filter starts on "All", as in the buy panels
  if (UiButton *b = dynamic_cast<UiButton *>(r.panel->getElementById(4021)))
    b->setLabel(this->resource_manager->getString(22900));
  // Funding - and + (hidden in the layout until the page shows them)
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

  // The category page: the branch of the chosen tab
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
  // The checkboxes say which categories are researched; unchecking the one
  // being worked on moves to another
  if (list) {
    bool changed = false;
    for (int row = 0; row < (int)r.listed.size(); row++) {
      int i = r.listed[row];
      if (list->isChecked(row) != b->enabled[i]) {
        b->enabled[i] = list->isChecked(row);
        changed = true;
      }
    }
    if (changed && (b->currentCategory < 0 || !b->enabled[b->currentCategory]))
      research.pick(*b);
  }

  if (!b->funding.empty()) {
    const ResearchFunding &f = b->funding[b->fundingLevel];
    std::string name = f.name;
    size_t at = name.find("%s");
    if (at != std::string::npos)
      name.replace(at, 2, formatPrice(f.cost));
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

void UiGameScreen::setupZooStatus() {
  for (Entry &e : this->entries)
    if (e.panel && e.id == 14)
      this->zooPanel = e.layout;
  if (!this->zooPanel)
    return;
  UiLayout *z = this->zooPanel;

  // Marketing levels: mktg.cfg names the level file (mktgnorm.cfg)
  if (IniReader *m = this->resource_manager->getIniReader("mktg.cfg")) {
    std::string file = m->get("marketing", "marketing");
    delete m;
    if (IniReader *levels = file.empty() ? nullptr
                                         : this->resource_manager->getIniReader(file)) {
      for (const std::string &key : levels->getList("marketing", "funding")) {
        int nameId = levels->getInt(key, "name", 0);
        this->marketing.push_back(
            {nameId ? this->resource_manager->getString(nameId) : key,
             levels->getInt(key, "cost", 0), levels->getInt(key, "benefit", 0)});
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
  // Admission: $0.25 a click
  if (UiButton *up = button(4195))
    up->onClick = [this] {
      this->zoo.admission = std::min(999.75, this->zoo.admission + 0.25);
    };
  if (UiButton *down = button(4191))
    down->onClick = [this] {
      this->zoo.admission = std::max(0.0, this->zoo.admission - 0.25);
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
      // To the nearest $0.25, as the arrows step
      double v = std::atof(typed.c_str());
      this->zoo.admission = std::clamp(std::round(v * 4) / 4, 0.0, 999.75);
    };
  }
}

void UiGameScreen::setZooInfo(const ZooInfo &info) {
  this->zoo = info;
  this->refreshZooStatus();
}

void UiGameScreen::setSim(ZooSim *sim) {
  this->sim = sim;
  this->chargeFunding();
}

// Research, conservation and marketing funding: a level's cost is per
// month, spread over the month's days (see ZooSim)
void UiGameScreen::chargeFunding() {
  if (!this->sim)
    return;
  Research &research = Research::get();
  for (int b = 0; b < (int)research.branches().size(); b++) {
    ResearchBranch &branch = research.branches()[b];
    double perMonth = branch.funding.empty() ? 0 : branch.funding[branch.fundingLevel].cost;
    this->sim->setMonthlyCost(ZooSim::Research, b, perMonth);
  }
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
    if (at != std::string::npos)
      s.replace(at, 2, formatPrice(m.cost));
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
  money(3928, std::lround(cash) + this->zoo.placedValue);           // zoo value

  // The graphs: a point a month
  if (history) {
    std::vector<UiGraph::Point> rating, donations, profit, attendance;
    for (const ZooSim::Month &m : *history) {
      std::string label = this->resource_manager->getString(22101 + m.month);
      rating.push_back({std::floor(m.rating), label});
      donations.push_back({m.lines[ZooSim::Donations], label});
      profit.push_back({m.total(), label});
      attendance.push_back({m.lines[ZooSim::Admissions], label});
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

  // Graphs: the page's Line / Bar buttons
  const int graphs[][3] = {{4150, 4151, 4152},
                           {13978, 13991, 13992},
                           {13967, 13994, 13995},
                           {13965, 13997, 13998}};
  for (const auto &g : graphs)
    if (UiGraph *graph = dynamic_cast<UiGraph *>(z->getElementById(g[0])))
      if (UiButton *bar = dynamic_cast<UiButton *>(z->getElementById(g[2])))
        graph->setBars(bar->isToggledOn());

  // Commerce list: the chosen sort's header is gold
  for (int i = 0; i < 5; i++) {
    UiText *label = dynamic_cast<UiText *>(z->getElementById(4184 + i));
    UiButton *sort = dynamic_cast<UiButton *>(z->getElementById(4175 + i));
    if (!label || !sort)
      continue;
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
  // it and opens its Building Information
  if (UiListBox *list = dynamic_cast<UiListBox *>(z->getElementById(4172))) {
    struct Row {
      int id;
      std::string name, type;
      int months, visitors;
      double profit, average;
    };
    std::vector<Row> rows;
    if (this->objects)
      for (const PlacedObjects::Object &o : this->objects->objects()) {
        if (o.label.empty() || o.price < 0)
          continue;
        int months = std::max(1, this->monthNumber - o.openedMonth + 1);
        double profit = o.income - o.upkeep;
        rows.push_back({o.id, o.label, o.typeName, months, o.visitorsTotal, profit, profit / months});
      }
    int sortBy = 0;
    for (int i = 0; i < 5; i++)
      if (UiButton *b = dynamic_cast<UiButton *>(z->getElementById(4175 + i)))
        if (b->isToggledOn())
          sortBy = i;
    std::stable_sort(rows.begin(), rows.end(), [sortBy](const Row &a, const Row &b) {
      switch (sortBy) {
      case 1: return a.months > b.months;
      case 2: return a.visitors > b.visitors;
      case 3: return a.profit > b.profit;
      case 4: return a.average > b.average;
      default: return a.type != b.type ? a.type < b.type : a.name < b.name;
      }
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
  for (Entry &e : this->entries)
    delete e.layout;
  delete this->filter.layout;
  delete this->dialog.layout;
}

UiElement *UiGameScreen::getElementById(int id) {
  for (Entry &e : this->entries)
    if (UiElement *found = e.layout->getElementById(id))
      return found;
  return nullptr;
}

bool UiGameScreen::isOverPanel(int x, int y) const {
  SDL_Point p = {x, y};
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
  this->refreshTerraform();
  this->refreshResearch();
  this->refreshZooStatus();
}

void UiGameScreen::setPanelOpen(int id, bool open) {
  for (Entry &e : this->entries)
    if (e.panel && e.id == id) {
      if (open && !e.open)
        e.openedAt = ++this->openCount;
      e.open = open;
    }
}

// An information window opens: the toolbar's panels close but an open
// list at the bottom right stays (the window goes above it), and only one
// information window shows at a time
void UiGameScreen::openInfo(int id) {
  std::vector<int> lists;
  for (int l : {30, 31, 16, 12})
    if (this->isPanelOpen(l))
      lists.push_back(l);
  this->showPanel(0, "");
  for (int l : lists) {
    this->setPanelOpen(l, true);
    for (UiButton *b : this->panelButtons)
      if (b->getActionTarget() == l)
        b->setToggledOn(true);
  }
  for (int p : {6, 9, 17, 20, 21})
    this->setPanelOpen(p, p == id);
}

bool UiGameScreen::closeTopmost() {
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
        SDL_Rect pr = this->panelRect(renderer, o, screen);
        return e.layout->computeRect(renderer, &pr);
      }
  SDL_Rect r = e.layout->computeRect(renderer, &parent);
  // An information window (animal, guest, staff, building) with a list
  // open at the bottom right (Exhibit/Show List, Staff List: x=right,
  // y=-366): just above the list, as the original stacks them
  if (e.id == 6 || e.id == 9 || e.id == 17 || e.id == 20 || e.id == 21)
    for (const Entry &o : this->entries) {
      if (!o.panel || !o.open || &o == &e)
        continue;
      IniReader *oi = o.layout->getIniReader();
      if (!oi || oi->get("layoutinfo", "x") != "right" || oi->get("layoutinfo", "y") != "-366")
        continue;
      SDL_Rect lr = this->panelRect(renderer, o, screen);
      r.x = parent.x + parent.w - r.w;
      r.y = lr.y - r.h;
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

void UiGameScreen::draw(SDL_Renderer *renderer, SDL_Rect *layout_rect) {
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
  for (Entry &e : this->entries) {
    if (!e.open)
      continue;
    if (!e.panel) {
      e.layout->draw(renderer, layout_rect);
      continue;
    }
    SDL_Rect r = this->panelRect(renderer, e, layout_rect);
    e.rect = r;
    e.layout->setAnchorRect(this->anchorRect(e, layout_rect));
    e.layout->draw(renderer, &r);
  }
  this->drawStaffPortrait(renderer);
  this->drawAnimalPortrait(renderer);
  this->drawMessage(renderer, layout_rect);
  this->drawTooltip(renderer, layout_rect);
}

// The button under the cursor with help (the topmost shown one), else over
// the map the tool's hint
std::string UiGameScreen::tooltipText() const {
  if (this->dialog.layout || this->mouseX < 0)
    return "";
  SDL_Point p = {this->mouseX, this->mouseY};
  const UiButton *found = nullptr;
  std::function<void(const UiElement *)> search = [&](const UiElement *e) {
    if (e->isHidden())
      return;
    if (const UiButton *b = dynamic_cast<const UiButton *>(e)) {
      SDL_Rect r = b->getLastRect();
      if (r.w > 0 && SDL_PointInRect(&p, &r) && b->getHelpId() > 0)
        found = b;
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
    int help = found->getHelpId();
    if (!found->isToggledOn()) {
      if (help == 1066)
        return this->resource_manager->getString(31065);
      if (help == 1067)
        return this->resource_manager->getString(31069);
      if (help == 1068)
        return this->resource_manager->getString(31070);
    }
    std::string tip = this->resource_manager->getString(30000 + help);
    return tip;
  }
  if (this->isOverHud(this->mouseX, this->mouseY))
    return "";
  return this->worldTip;
}

// A box with a gold edge, the help in white outlined in black, wrapped,
// centred under the cursor and kept on the screen
void UiGameScreen::drawTooltip(SDL_Renderer *renderer, SDL_Rect *layout_rect) {
  if (SDL_GetTicks() < this->mouseStill + 600)
    return;
  std::string text = this->tooltipText();
  if (text.empty())
    return;
  const int font = 7002, size = 7003, maxWidth = 285;
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
  SDL_SetRenderDrawColor(renderer, 255, 227, 85, 255);
  SDL_RenderFillRect(renderer, &box);
  SDL_Rect inner = {box.x + 1, box.y + 1, box.w - 2, box.h - 2};
  SDL_SetRenderDrawColor(renderer, 82, 75, 43, 255);
  SDL_RenderFillRect(renderer, &inner);
  SDL_Rect fill = {box.x + 2, box.y + 2, box.w - 4, box.h - 4};
  SDL_SetRenderDrawColor(renderer, 115, 101, 49, 255);
  SDL_RenderFillRect(renderer, &fill);
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
        SDL_RenderCopy(renderer, black, nullptr, &d);
      }
    }
    if (white) {
      SDL_Rect d = {x, y, tw, th};
      SDL_RenderCopy(renderer, white, nullptr, &d);
    }
    y += lineH;
  }
}

void UiGameScreen::postMessage(const std::string &text, int kind, Subject subject, int id) {
  Uint32 now = SDL_GetTicks();
  if (kind <= 2)
    for (const LoggedMessage &m : this->messageLog)
      if (m.text == text && now - m.at < 60000)
        return;
  static const SDL_Color colours[3] = {{255, 255, 255, 255}, {0, 255, 0, 255}, {255, 0, 0, 255}};
  this->showMessage(text, colours[kind % 3]);
  this->messageLog.insert(this->messageLog.begin(), {text, kind, subject, id, now});
  if (this->messageLog.size() > 25)
    this->messageLog.resize(25);
  this->messageSerial++;
}

void UiGameScreen::showMessage(const std::string &text, SDL_Color color, Uint32 ms) {
  this->message = text;
  this->messageColor = color;
  this->messageUntil = SDL_GetTicks() + ms;
}

// The bar's art is drawn with the screen (shown while there's a message);
// the message goes on it, centred (gamescrn.lyt: font 7002, size 7003)
void UiGameScreen::drawMessage(SDL_Renderer *renderer, SDL_Rect *layout_rect) {
  if (!this->messageBar || this->isPanelOpen(29)) // (the list shows it instead)
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
  SDL_Texture *t = this->resource_manager->getStringTexture(renderer, 7002, text, color, 7003);
  if (!t)
    return;
  int w = 0, h = 0;
  this->resource_manager->getTextSize(t, &w, &h);
  SDL_Rect dst = {bar.x + (bar.w - w) / 2, bar.y + (bar.h - h) / 2, w, h};
  SDL_RenderCopy(renderer, t, nullptr, &dst);
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
  if (!owner) {
    this->filter.open = false;
    return;
  }
  SDL_Rect anchor = this->anchorRect(*owner, layout_rect);
  SDL_Rect r = this->filter.layout->computeRect(renderer, &anchor);
  this->filter.layout->draw(renderer, &r);
}

UiAction UiGameScreen::handleInputs(std::vector<Input> &inputs) {
  // Where the cursor rests (for tooltips): a move or a click starts again
  for (const Input &in : inputs)
    if (in.type == InputType::POSITIONED) {
      if (in.event == InputEvent::CURSOR_MOVE &&
          (in.position.x != this->mouseX || in.position.y != this->mouseY))
        this->mouseStill = SDL_GetTicks();
      else if (in.event != InputEvent::CURSOR_MOVE)
        this->mouseStill = SDL_GetTicks() + 100000; // none until it moves
      this->mouseX = in.position.x;
      this->mouseY = in.position.y;
    }
  // A dialog takes every input while it is up
  if (this->dialog.layout) {
    this->handleDialog(inputs);
    return UiAction::NONE;
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
  this->refreshTerraform();
  this->refreshResearch();
  this->refreshZooStatus();
  this->refreshExhibits();
  this->refreshFilterPanel();
  this->refreshStaff();
  this->refreshAnimal();
  this->refreshGuest();
  this->refreshBuilding();
  this->refreshLists();

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

// ----------------------------------------------------------------------------
// The map under the mouse, the fence tool and the bulldozer
// ----------------------------------------------------------------------------
bool UiGameScreen::isOverHud(int x, int y) const {
  if (this->dialog.layout || this->filter.open)
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
  static const char *placed[] = {"animals", "shelters", "toys", "showtoys", "structures", "scenery", "foliage", "rocks"};
  for (BuyPanel &p : this->buyPanels) {
    bool drop = false;
    for (const char *c : placed)
      drop = drop || p.category == c;
    if (!drop || !p.region || p.region->getSelected() < 0)
      continue;
    p.region->setSelected(-1);
    this->showItem(p, -1);
  }
}

std::string UiGameScreen::objectToolFile(int &cost, int &facing) const {
  cost = 0;
  facing = this->facing;
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
      return p.items[i]->file;
    }
  }
  return "";
}

std::string UiGameScreen::animalToolFile(bool &female) const {
  female = this->animalPickFemale;
  if (!this->isPanelOpen(3))
    return "";
  for (const BuyPanel &p : this->buyPanels)
    if (p.id == 3 && p.category == "animals")
      return this->animalPick;
  return "";
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
  if (this->dialog.layout) {
    this->dialogQueue.push_back([=] { this->popup(image, message); });
    return;
  }
  IniReader *ini = this->resource_manager->getIniReader("ui/confirmm.lyt");
  if (!ini)
    return;
  this->dialog.layout = new UiLayout(ini, this->resource_manager);
  this->dialog.yes = nullptr;
  UiLayout *d = this->dialog.layout;
  if (UiText *m = dynamic_cast<UiText *>(d->getElementById(45206)))
    m->setMessage(message);
  if (UiImage *icon = dynamic_cast<UiImage *>(d->getElementById(45204)))
    if (!image.empty())
      icon->setImage(image);
  for (int id : {45210, 45211, 45213}) // just OK
    if (UiElement *e = d->getElementById(id))
      e->setHidden(true);
  if (UiButton *ok = dynamic_cast<UiButton *>(d->getElementById(45212)))
    ok->onClick = [this] { this->dialog.submit = true; };
}

void UiGameScreen::askConfirm(const std::string &message, std::function<void()> yes) {
  if (this->dialog.layout) {
    this->dialogQueue.push_back([=] { this->askConfirm(message, yes); });
    return;
  }
  IniReader *ini = this->resource_manager->getIniReader("ui/confirm.lyt");
  if (!ini)
    return;
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
  if (this->dialog.cancel) {
    this->closeDialog();
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
      if (!this->fences || typed.empty())
        return;
      if (Fences::Exhibit *ex = this->fences->exhibit(this->shownExhibit))
        ex->name = typed;
      this->shownListCount = -1; // relist
    };
  if (UiButton *drain = dynamic_cast<UiButton *>(z->getElementById(4885)))
    drain->onClick = [this] {
      if (this->fences)
        this->fences->drainExhibit(this->shownExhibit);
    };
  for (int id : {4869, 4870}) // salt, fresh: fill it again, for the water
    if (UiButton *fill = dynamic_cast<UiButton *>(z->getElementById(id)))
      fill->onClick = [this, id] {
        if (!this->fences)
          return;
        bool salt = id == 4869;
        int cost = this->fences->fillCost(this->shownExhibit, salt);
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
        // (the wall's price shown cut to dollars, charged rounded: $363.50
        // shows $363 and takes $364, as the original)
        int cost = s.wall ? static_cast<int>(std::lround(this->fences->wallStepCost(id)))
                          : this->fences->baseStepCost(id);
        if (s.wall)
          this->fences->adjustWall(id, s.step);
        else
          this->fences->adjustBase(id, s.step);
        if (this->sim && cost > 0)
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
      if (!this->fences || typed.empty())
        return;
      if (Fences::Filter *f = this->fences->filter(this->shownFilter))
        f->name = typed;
    };
  if (UiButton *sell = dynamic_cast<UiButton *>(z->getElementById(4906)))
    sell->onClick = [this] {
      if (!this->fences || !this->fences->filter(this->shownFilter))
        return;
      int refund = this->fences->filterType().cost / 2;
      this->fences->removeFilter(this->shownFilter);
      if (this->sim && refund > 0)
        this->sim->earn(ZooSim::Construction, refund);
      this->shownFilter = -1;
      this->setPanelOpen(46, false);
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
  const Fences::FilterType &k = this->fences->filterType();
  auto text = [&](int eid, const std::string &s) {
    if (UiText *t = dynamic_cast<UiText *>(z->getElementById(eid)))
      if (!t->isBeingEdited())
        t->setText(s);
  };
  auto money = [](double v) {
    char b[32];
    std::snprintf(b, sizeof b, "$%.2f", v);
    return std::string(b);
  };
  text(4904, f->name);
  if (UiImage *pic = dynamic_cast<UiImage *>(z->getElementById(4903)))
    pic->setImage("objects/filter/off/off");
  text(4913, this->resource_manager->getString(f->health > k.decayedHealth ? 4913 : 4914));
  int months = std::max(0, this->fences->month() - f->placedMonth);
  std::string ago = this->resource_manager->getString(4916);
  size_t at = ago.find("%s");
  if (at != std::string::npos)
    ago.replace(at, 2, std::to_string(months));
  text(4916, ago);
  text(4918, std::to_string(months));
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
    auto ask = [this](StaffRequest r) {
      return [this, r] {
        if (this->staffRequest && this->shownStaff >= 0)
          this->staffRequest(r, this->shownStaff);
      };
    };
    if (UiButton *b = dynamic_cast<UiButton *>(z->getElementById(3570)))
      b->onClick = ask(StaffRequest::PickUp);
    if (UiButton *b = dynamic_cast<UiButton *>(z->getElementById(3606)))
      b->onClick = [this] {
        if (this->staffRequest && this->shownStaff >= 0)
          this->staffRequest(StaffRequest::Fire, this->shownStaff);
        this->shownStaff = -1;
        this->setPanelOpen(17, false);
        this->staffListCount = -1;
      };
    if (UiButton *b = dynamic_cast<UiButton *>(z->getElementById(3578)))
      b->onClick = ask(StaffRequest::Assign);
    if (UiButton *b = dynamic_cast<UiButton *>(z->getElementById(3515)))
      b->onClick = [this] {
        this->tracking = !this->tracking;
        if (this->staffRequest)
          this->staffRequest(StaffRequest::Track, this->tracking ? this->shownStaff : -1);
      };
    if (UiText *name = dynamic_cast<UiText *>(z->getElementById(3502)))
      name->onCommit = [this](const std::string &typed) {
        if (!this->staff || typed.empty())
          return;
        if (Staff::Member *m = this->staff->member(this->shownStaff))
          m->name = typed;
        this->staffListCount = -1;
      };
    // A maintenance worker's duties: ticks
    if (UiListBox *duties = dynamic_cast<UiListBox *>(z->getElementById(3587))) {
      duties->clear();
      for (int id : {3530, 3531, 3532, 3533})
        duties->addItem(this->resource_manager->getString(id));
    }
    // A keeper's exhibits: clear the picked one, or all
    if (UiButton *b = dynamic_cast<UiButton *>(z->getElementById(3579)))
      b->onClick = [this] {
        UiListBox *l = this->staffPanel ? dynamic_cast<UiListBox *>(this->staffPanel->getElementById(3569)) : nullptr;
        Staff::Member *m = this->staff ? this->staff->member(this->shownStaff) : nullptr;
        if (l && m && l->getSelectedIndex() >= 0 && l->getSelectedIndex() < (int)m->exhibits.size())
          m->exhibits.erase(m->exhibits.begin() + l->getSelectedIndex());
      };
    if (UiButton *b = dynamic_cast<UiButton *>(z->getElementById(3580)))
      b->onClick = [this] {
        if (Staff::Member *m = this->staff ? this->staff->member(this->shownStaff) : nullptr)
          m->exhibits.clear();
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
  this->tracking = false;
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
  // Keepers with no exhibits open on Job Assignment (as the original);
  // the rest on Status
  UiButton *tab = dynamic_cast<UiButton *>(
      z->getElementById(keeperLike && m->exhibits.empty() ? 3564 : 3562));
  if (tab)
    tab->choose();
  z->syncTabs();
  if (UiListBox *duties = dynamic_cast<UiListBox *>(z->getElementById(3587)))
    for (int i = 0; i < 4; i++)
      duties->setChecked(i, m->duties[i]);
  this->refreshStaff();
}

void UiGameScreen::refreshStaff() {
  if (!this->staff)
    return;
  // Staff Information
  if (UiLayout *z = this->staffPanel) {
    Staff::Member *m = this->staff->member(this->shownStaff);
    if (this->isPanelOpen(17) && !m)
      this->setPanelOpen(17, false);
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
      text(3503, formatPrice(t.salary));
      text(3505, this->staff->dutyText(*m));
      if (UiListBox *duties = dynamic_cast<UiListBox *>(z->getElementById(3587)))
        for (int i = 0; i < 4; i++)
          m->duties[i] = duties->isChecked(i);
      if (UiListBox *l = dynamic_cast<UiListBox *>(z->getElementById(3569)))
        if ((int)l->getItemCount() != (int)m->exhibits.size()) {
          l->clear();
          for (int ex : m->exhibits)
            if (const Fences::Exhibit *e = this->fences ? this->fences->exhibit(ex) : nullptr)
              l->addItem(e->name);
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
    auto keeperLike = [&](const Staff::Member &m) {
      Staff::Kind k = kindOf(m);
      return k == Staff::Kind::Keeper || k == Staff::Kind::Scientist || k == Staff::Kind::Trainer;
    };
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
    bool relist = (int)shown.size() != this->staffListCount || shown != this->listedStaff;
    if (relist) {
      list->clear();
      for (int id : shown) {
        const Staff::Member *m = this->staff->member(id);
        const Staff::Type &t = this->staff->types()[m->type];
        list->addItem(m->name, "", t.listImage[m->female ? 1 : 0]);
      }
      this->listedStaff = shown;
      this->staffListCount = (int)shown.size();
      list->setSelectedIndex(-1);
      for (size_t i = 0; i < shown.size(); i++)
        if (shown[i] == this->shownStaff)
          list->setSelectedIndex((int)i);
    }
    int sel = list->getSelectedIndex();
    if (sel >= 0 && sel < (int)this->listedStaff.size() && this->listedStaff[sel] != this->shownStaff) {
      int id = this->listedStaff[sel];
      if (this->staffRequest)
        this->staffRequest(StaffRequest::Select, id);
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
}

// The filters (zoo.exe 0x4537fe, 0x4f5d7f), T = cUIThreshold (75 in both
// animals.ai and guests.ai): angry at happiness 2T - 201 or less (-51 on
// the -100..100 scale), hungry and the other needs over T; unhappy below
// its species' cHabitatPreference (outside an exhibit: 0, so unhappy)
static constexpr int kUiThreshold = 75;

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
  // arrow; the Messages button lit while it's open
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
        // picked (zoo.exe 0x47fa78)
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
      std::string sx = this->resource_manager->getString(this->animals->types()[x->type].nameId);
      std::string sy = this->resource_manager->getString(this->animals->types()[y->type].nameId);
      if (sx != sy)
        return sx < sy;
      if (x->name != y->name)
        return x->name < y->name;
      return a < b;
    });
    text(z, 3835, this->resource_manager->getString(kAnimalNames[this->animalFilter]));
    if (UiListBox *list = dynamic_cast<UiListBox *>(z->getElementById(3805))) {
      int picked = fill(list, ids, this->listedAnimals, this->litAnimal, this->animals->selected, [this](int id) {
        const Animals::Member *m = this->animals->member(id);
        return std::make_pair(m->name, this->animals->types()[m->type].listImage);
      });
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
  if (UiButton *b = dynamic_cast<UiButton *>(z->getElementById(3197)))
    b->onClick = [this] {
      const Animals::Member *m = this->animals ? this->animals->member(this->shownAnimal) : nullptr;
      if (m && m->exhibit >= 0)
        this->showExhibit(m->exhibit);
    };
  if (UiButton *b = dynamic_cast<UiButton *>(z->getElementById(3122)))
    b->onClick = [this] {
      if (this->shownAnimal >= 0)
        this->showAdvice(this->shownAnimal);
    };
  if (UiButton *b = dynamic_cast<UiButton *>(z->getElementById(3111)))
    b->onClick = [this] {
      const Animals::Member *m = this->animals ? this->animals->member(this->shownAnimal) : nullptr;
      if (!m)
        return;
      int id = m->id;
      std::string ask = this->resource_manager->getString(154);
      size_t at = ask.find("%s");
      if (at != std::string::npos)
        ask.replace(at, 2, m->name);
      ask += formatPrice(this->animals->refund(id));
      this->askConfirm(ask, [this, id] {
        if (this->animalRequest)
          this->animalRequest(AnimalRequest::Sell, id);
        if (this->shownAnimal == id) {
          this->shownAnimal = -1;
          this->setPanelOpen(6, false);
        }
      });
    };
  if (UiText *name = dynamic_cast<UiText *>(z->getElementById(3102)))
    name->onCommit = [this](const std::string &typed) {
      if (!this->animals || typed.empty())
        return;
      if (Animals::Member *m = this->animals->member(this->shownAnimal))
        m->name = typed;
    };
}

void UiGameScreen::showBuilding(int id) {
  PlacedObjects::Object *o = this->objects ? this->objects->byId(id) : nullptr;
  if (!o)
    return;
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
    // (its cLowCost to cHighCost, $30 at most: zoo.exe 0x611b43, 0x5a5bbc)
    float lo = 1, hi = 1;
    if (IniReader *ai = this->resource_manager->getIniReader(PlacedObjects::fileOf(*o))) {
      std::string l = ai->get("characteristics/floats", "clowcost"), h = ai->get("characteristics/floats", "chighcost");
      lo = l.empty() ? 1.0f : static_cast<float>(std::atof(l.c_str()));
      hi = h.empty() ? 1.0f : static_cast<float>(std::atof(h.c_str()));
      delete ai;
    }
    if (UiButton *up = dynamic_cast<UiButton *>(z->getElementById(4557)))
      up->onClick = [this, hi] {
        if (PlacedObjects::Object *b = this->objects->byId(this->shownBuilding))
          b->price = std::min({b->price + 0.25f, 30.0f, hi});
      };
    if (UiButton *dn = dynamic_cast<UiButton *>(z->getElementById(4556)))
      dn->onClick = [this, lo] {
        if (PlacedObjects::Object *b = this->objects->byId(this->shownBuilding))
          b->price = std::max({b->price - 0.25f, 0.0f, lo});
      };
  }
  if (UiButton *sell = dynamic_cast<UiButton *>(z->getElementById(commerce ? 4516 : 4616)))
    sell->onClick = [this, commerce] {
      int id = this->shownBuilding;
      if (this->sellBuilding)
        this->sellBuilding(id);
      this->setPanelOpen(commerce ? 21 : 20, false);
      this->shownBuilding = -1;
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
  auto money = [](double v) {
    char b[32];
    std::snprintf(b, sizeof b, v < 0 ? "-$%.0f" : "$%.0f", std::fabs(v));
    return std::string(b);
  };
  this->refreshColours(z, o, open21);
  int months = std::max(1, this->monthNumber - o->openedMonth + 1);
  if (open21) {
    text(4502, o->label);
    char p[32];
    std::snprintf(p, sizeof p, "$%.2f", o->price);
    text(4555, p);
    text(4531, money(o->income));
    text(4533, money(o->upkeep));
    text(4535, money(o->income - o->upkeep));
    text(4559, money(o->income / months));
    text(4561, money(o->upkeep / months));
    text(4591, money((o->income - o->upkeep) / months));
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
  // (Track starts off: on, its yellow lit art filled the portrait's circle)
  if (UiButton *track = dynamic_cast<UiButton *>(z->getElementById(3415)))
    track->setToggledOn(false);
  if (UiButton *tab = dynamic_cast<UiButton *>(z->getElementById(3462)))
    tab->choose();
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
  bar(3404, (g->happiness + 100) / 2);
  bar(3406, 100 - g->thirst);
  bar(3408, 100 - g->hunger);
  bar(3410, 100 - g->bathroom);
  bar(3412, 100 - g->tired);
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
  // It opens on Status
  if (UiButton *tab = dynamic_cast<UiButton *>(this->animalPanel->getElementById(3162)))
    tab->choose();
  this->animalPanel->syncTabs();
  this->refreshAnimal();
}

void UiGameScreen::refreshAnimal() {
  if (!this->animals)
    return;
  UiLayout *z = this->animalPanel;
  const Animals::Member *m = this->animals->member(this->shownAnimal);
  if (this->isPanelOpen(6) && !m)
    this->setPanelOpen(6, false);
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
  bar(3104, static_cast<float>(Animals::shownHappiness(*m)));
  bar(3108, m->food);
  bar(3106, static_cast<float>(this->animals->shownHealth(*m)));
  bar(3110, this->animalSuitability ? static_cast<float>(this->animalSuitability(m->id)) : 0.0f);
  if (UiImage *sex = dynamic_cast<UiImage *>(z->getElementById(3172))) {
    std::string want = m->female ? "ui/animal/icfemale.tga" : "ui/animal/icmale.tga";
    if (this->animalSexImage != want) {
      sex->setImage(want);
      this->animalSexImage = want;
    }
  }
  // Thoughts: the latest five guests had about it
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
// over a list of what would make it happier, problems in red
void UiGameScreen::showAdvice(int id) {
  const Animals::Member *m = this->animals ? this->animals->member(id) : nullptr;
  UiLayout *z = nullptr;
  for (Entry &e : this->entries)
    if (e.panel && e.id == 28)
      z = e.layout;
  if (!m || !z)
    return;
  this->setPanelOpen(28, true);
  // The patched layout lists the zookeeper's, the scientist's and the
  // marine specialist's pictures: a zoo animal's is the zookeeper's
  if (UiImage *bk = dynamic_cast<UiImage *>(z->getElementById(26000)))
    bk->setImage("ui/keprinfo/kinfobk/kinfobk");
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
  for (const Entry &e : this->entries)
    if (e.panel && e.id == 6 && e.open)
      if (Animation *a = m->boxed ? this->animals->crate() : this->animals->art(*m, "stand"))
        a->drawAnchored(renderer, static_cast<float>(e.rect.x + 34),
                        static_cast<float>(e.rect.y + 62), CompassDirection::SE, nullptr, 0);
}

void UiGameScreen::showExhibit(int id) {
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
  if (id != this->shownExhibit) {
    this->shownExhibit = id;
    if (ex && this->centreOn) {
      float cx = 0, cy = 0;
      for (auto [tx, ty] : ex->tiles) {
        cx += tx + 0.5f;
        cy += ty + 0.5f;
      }
      cx /= ex->tiles.size();
      cy /= ex->tiles.size();
      this->centreOn(cx, cy);
    }
    // The page: Modify Tank for a tank, else its status
    UiButton *tankTab = dynamic_cast<UiButton *>(element(4365));
    UiButton *statusTab = dynamic_cast<UiButton *>(element(4310));
    if (tankTab)
      tankTab->setDisabled(!ex || !ex->tank);
    if (ex && ex->tank && tankTab)
      tankTab->choose();
    else if (statusTab)
      statusTab->choose();
    z->syncTabs();
  }
  // Nothing picked: the information is empty
  for (int sub : {700, 701, 702, 703, 704})
    if (UiElement *l = element(sub))
      if (!ex)
        l->setHidden(true);
  if (UiElement *name = element(4304))
    name->setHidden(!ex);
  if (!ex)
    return;
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
    int pick = animalList->getSelectedIndex();
    if (pick != this->litExhibitAnimal && pick >= 0 && pick < (int)in.size()) {
      int aid = in[pick];
      this->litExhibitAnimal = pick;
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
  // Tank Adjustment: the wall and base arrows (greyed where they can go no
  // further) and a step's price; filled, the water's salinity and Drain;
  // drained, Fill Tank with salt or fresh at their prices
  bool tank = ex->groundHeight != ex->floorHeight;
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
  // (the amounts alone, as the original shows them)
  text(4877, formatPrice(static_cast<int>(this->fences->wallStepCost(id))));
  text(4882, formatPrice(this->fences->baseStepCost(id)));
  for (int eid : {4867, 4868, 4883, 4885})
    show(eid, full);
  for (int eid : {4881, 4869, 4870, 4891, 4893, 4871, 4872})
    show(eid, tank && !full);
  grey(4885, false);
  text(4868, this->resource_manager->getString(ex->salt ? 4888 : 4889));
  text(4871, formatPrice(this->fences->fillCost(id, true)));
  text(4872, formatPrice(this->fences->fillCost(id, false)));
}
