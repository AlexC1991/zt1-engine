#include "UiGameScreen.hpp"

#include <algorithm>
#include <functional>
#include <set>

#include "../ItemCatalog.hpp"
#include "../Research.hpp"
#include "../Utils.hpp"
#include "UiButton.hpp"
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
  this->setupTerraform();
  this->setupResearch();

  // The game menu's Main Menu and Exit Game (their layout gives them no
  // action; the game does it)
  for (UiAction a : {UiAction::GAME_MAIN_MENU, UiAction::GAME_EXIT})
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
    p.region->onSelect = [this, self](int index) { this->showItem(*self, index); };
    // Right turns it clockwise (SE, SW, NW, NE), left back
    if (UiButton *left = dynamic_cast<UiButton *>(
            p.rotateLeft ? p.layout->getElementById(p.rotateLeft) : nullptr))
      left->onClick = [this] { this->rotateItems(-1); };
    if (UiButton *right = dynamic_cast<UiButton *>(
            p.rotateRight ? p.layout->getElementById(p.rotateRight) : nullptr))
      right->onClick = [this] { this->rotateItems(+1); };
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
}

// An item's icon for the current facing (items with fewer icons than
// facings, like the animals, keep theirs)
const std::string &UiGameScreen::facingIcon(const CatalogItem *item) const {
  if (item->icons.empty())
    return item->icon;
  return item->icons[this->facing % item->icons.size()];
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
  t.brushIndex = t.brushes.empty() ? 0 : (int)t.brushes.size() - 1;
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
  text(3305, "$0");
  for (int id : {3302, 3303}) // accept, undo: nothing to accept yet
    if (UiElement *e = element(id))
      e->setDisabled(true);

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

UiGameScreen::~UiGameScreen() {
  for (Entry &e : this->entries)
    delete e.layout;
  delete this->filter.layout;
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
    for (UiButton *tab : p.tabs)
      tab->setToggledOn(Utils::string_to_lower(tab->getStringData()) == category);
    this->refreshBuyPanel(p);
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
    e.rect = r;
    e.layout->setAnchorRect(this->anchorRect(e, layout_rect));
    e.layout->draw(renderer, &r);
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
  if (!owner) {
    this->filter.open = false;
    return;
  }
  SDL_Rect anchor = this->anchorRect(*owner, layout_rect);
  SDL_Rect r = this->filter.layout->computeRect(renderer, &anchor);
  this->filter.layout->draw(renderer, &r);
}

UiAction UiGameScreen::handleInputs(std::vector<Input> &inputs) {
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
