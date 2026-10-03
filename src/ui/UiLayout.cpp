#include "UiLayout.hpp"
#include "../RenderSettings.hpp"

#include <algorithm>
#include <cstdlib>
#include <functional>
#include <memory>

#include "UiButton.hpp"
#include "UiImage.hpp"
#include "UiListBox.hpp"
#include "UiMiniMap.hpp"
#include "UiStatusImage.hpp"
#include "UiScrollBar.hpp"
#include "UiScrollingRegion.hpp"
#include "UiText.hpp"

UiLayout::UiLayout(IniReader *ini_reader, ResourceManager *resource_manager) {
  this->ini_reader = ini_reader;
  this->resource_manager = resource_manager;

  this->name = "layoutinfo";
  this->id = ini_reader->getInt(this->name, "id", 0);
  this->anchor = ini_reader->getInt(this->name, "anchor", 0);
  this->layer = ini_reader->getInt(this->name, "layer", 1);

  this->process_sections(ini_reader, resource_manager);
}

UiLayout::UiLayout(IniReader *ini_reader, ResourceManager *resource_manager,
                   std::string name) {
  // The section here only says which file (and the layout's state); the
  // file has the elements and its own [LayoutInfo] (id, place, layer)
  this->resource_manager = resource_manager;
  this->name = "layoutinfo";
  this->nested = true;
  std::string path = ini_reader->get(name, "layout");
  this->ini_reader =
      path.empty() ? nullptr : resource_manager->getIniReader(path);
  if (this->ini_reader == nullptr) {
    SDL_Log("UiLayout: layout '%s' of section %s not found", path.c_str(),
            name.c_str());
    return;
  }
  this->id = this->ini_reader->getInt("layoutinfo", "id", 0);
  this->layer = ini_reader->getInt(
      name, "layer", this->ini_reader->getInt("layoutinfo", "layer", 1));
  this->anchor = this->ini_reader->getInt("layoutinfo", "anchor", 0);

  this->process_sections(this->ini_reader, resource_manager);
}

UiLayout::~UiLayout() {
  for (UiElement *element : this->children) {
    delete element;
  }
  this->children.clear();

  // IniReader is created with new in ResourceManager::getIniReader, so delete.
  if (this->ini_reader != nullptr) {
    delete this->ini_reader;
    this->ini_reader = nullptr;
  }
}

void UiLayout::draw(SDL_Renderer *renderer, SDL_Rect *layout_rect) {
  if (renderer == nullptr)
    return;

  SDL_Rect window_rect = {0, 0, 0, 0};
  if (layout_rect == nullptr) {
    if (!window) {
      this->window = SDL_RenderGetWindow(renderer);
    }
    SDL_GetWindowSize(this->window, &window_rect.w, &window_rect.h);
    layout_rect = &window_rect;
  }
  if (this->ini_reader == nullptr)
    return;
  // A layout inside a layout sits where its [LayoutInfo] puts it: in its
  // anchor's rect when it shares its parent's anchor, else in the parent
  SDL_Rect own_rect;
  if (this->nested) {
    SDL_Rect *base = this->has_anchor_rect ? &this->anchor_rect : layout_rect;
    own_rect = this->getRect(this->ini_reader->getSection("layoutinfo"), base);
    layout_rect = &own_rect;
  }
  this->last_rect = *layout_rect;

  // Like the original: every element drawn in layer order across the whole
  // layout (not parent-then-children), each positioned inside its anchor.
  // Within a layer, elements anchored to another are drawn before it (the
  // HUD's rating buttons cover the left end of their bars in the original).
  struct Item {
    UiElement *element;
    SDL_Rect parent;
    int depth;
  };
  std::vector<Item> items;
  int maxDepth = 0;
  std::function<void(UiElement *, SDL_Rect, int)> visit =
      [&](UiElement *e, SDL_Rect parent, int depth) {
        if (e->isHidden())
          return;
        items.push_back({e, parent, depth});
        maxDepth = std::max(maxDepth, depth);
        if (dynamic_cast<UiLayout *>(e))
          return;
        SDL_Rect r = e->computeRect(renderer, &parent);
        for (UiElement *c : e->getChildren())
          visit(c, r, depth + 1);
      };
  for (UiElement *child : this->children)
    visit(child, *layout_rect, 0);

  int maxLayer = 0;
  for (const Item &it : items)
    maxLayer = std::max(maxLayer, it.element->getLayer());
  // The text pass (GPU FSR draws text after the upscaled art) only needs
  // the elements that have text
  auto hasText = [](UiElement *e) {
    return dynamic_cast<UiText *>(e) || dynamic_cast<UiButton *>(e) ||
           dynamic_cast<UiListBox *>(e) || dynamic_cast<UiLayout *>(e);
  };
  for (int layer = 0; layer <= maxLayer; layer++) {
    for (int depth = maxDepth; depth >= 0; depth--)
    for (Item &it : items) {
      if (it.element->getLayer() != layer || it.depth != depth)
        continue;
      if (!RenderSettings::drawsUiArt() && !hasText(it.element))
        continue;
      if (UiLayout *sub = dynamic_cast<UiLayout *>(it.element))
        if (this->has_anchor_rect && sub->getAnchor() != 0 &&
            sub->getAnchor() == this->getAnchor())
          sub->setAnchorRect(this->anchor_rect);
      it.element->draw(renderer, &it.parent);
    }
  }
}

void UiLayout::process_sections(IniReader *ini_reader,
                                ResourceManager *resource_manager) {
  this->id = ini_reader->getInt(name, "id", 0);
  this->layer_count = ini_reader->getInt(name, "layer", 0);

  // 2-pass build:
  // Pass 1: create everything and add root-anchored elements.
  // Pass 2: attach anchored elements once the tree exists.
  std::vector<UiElement *> pending_anchored;
  std::vector<std::vector<int>> radio_sets;

  for (std::string section : ini_reader->getSections()) {
    if (section == this->name || section == "layoutinfo") {
      continue;
    }

    UiElement *new_element = nullptr;
    std::string element_type = ini_reader->get(section, "type");

    // Map preview boxes sometimes have odd metadata; force UiImage for known
    // names.
    if (section == "smap" || section == "fmap" || section == "map_preview") {
      new_element =
          (UiElement *)new UiImage(ini_reader, resource_manager, section);
    } else if (element_type == "UIImage") {
      new_element =
          (UiElement *)new UiImage(ini_reader, resource_manager, section);
    } else if (element_type == "UIButton") {
      new_element =
          (UiElement *)new UiButton(ini_reader, resource_manager, section);
    } else if (element_type == "UIText" || element_type == "UIEditableText") {
      // UIEditableText (e.g. the expansions' Starting Cash box) is shown as
      // plain text for now; typing into it isn't supported yet
      new_element =
          (UiElement *)new UiText(ini_reader, resource_manager, section);
    } else if (element_type == "UIListBox") {
      new_element =
          (UiElement *)new UiListBox(ini_reader, resource_manager, section);
    } else if (element_type == "UILayout") {
      new_element =
          (UiElement *)new UiLayout(ini_reader, resource_manager, section);
    } else if (element_type == "UIStatusImage") {
      new_element =
          (UiElement *)new UiStatusImage(ini_reader, resource_manager, section);
    } else if (element_type == "ZTMiniMap") {
      new_element =
          (UiElement *)new UiMiniMap(ini_reader, resource_manager, section);
    } else if (element_type == "UIRadioSet") {
      // Groups toolbar buttons so only one is selected; no visuals. Linked
      // up once every button exists.
      std::vector<int> ids;
      for (const std::string &id : ini_reader->getList(section, "button"))
        ids.push_back(std::atoi(id.c_str()));
      radio_sets.push_back(ids);
      continue;
    } else if (element_type == "UIScrollingRegion") {
      new_element = (UiElement *)new UiScrollingRegion(ini_reader,
                                                       resource_manager, section);
    } else if (element_type == "UIScrollBar") {
      new_element =
          (UiElement *)new UiScrollBar(ini_reader, resource_manager, section);
    } else {
      if (element_type.empty()) {
        SDL_Log("Could not determine type of section %s", section.c_str());
      } else {
        SDL_Log("Unknown UI element type '%s' in section %s",
                element_type.c_str(), section.c_str());
      }
    }

    if (!new_element)
      continue;

    int stateFlags = 0;
    for (const std::string &st : ini_reader->getList(section, "state"))
      stateFlags |= std::atoi(st.c_str());
    if (stateFlags == 0)
      stateFlags = ini_reader->getInt(section, "state", 0);
    new_element->setStateFlags(stateFlags);

    int anchorId = new_element->getAnchor();
    if (anchorId == 0 || anchorId == this->id) {
      this->children.push_back(new_element);
    } else {
      pending_anchored.push_back(new_element);
    }
  }

  // Pass 2: attach anchored elements. An element can anchor to another
  // anchored element, so repeat until nothing more attaches.
  bool progress = true;
  while (!pending_anchored.empty() && progress) {
    progress = false;
    for (auto it = pending_anchored.begin(); it != pending_anchored.end();) {
      UiElement *anchorTarget = this->getElementById((*it)->getAnchor());
      if (anchorTarget != nullptr) {
        anchorTarget->addChild(*it);
        it = pending_anchored.erase(it);
        progress = true;
      } else {
        ++it;
      }
    }
  }
  for (UiElement *child : pending_anchored) {
    SDL_Log("Anchor id %d was not found for element id=%d name='%s'",
            child->getAnchor(), child->getId(), child->getName().c_str());
    // Fallback: attach to root so it still exists
    this->children.push_back(child);
  }

  // The layout draws every element itself, in layer order
  std::function<void(UiElement *)> mark = [&](UiElement *e) {
    if (dynamic_cast<UiLayout *>(e))
      return; // nested layouts draw their own elements
    e->setLayoutDrawsChildren(true);
    for (UiElement *c : e->getChildren())
      mark(c);
  };
  for (UiElement *child : this->children)
    mark(child);

  // Radio sets: their buttons share one group
  for (const std::vector<int> &ids : radio_sets) {
    auto group = std::make_shared<std::vector<UiButton *>>();
    for (int id : ids)
      if (UiButton *b = dynamic_cast<UiButton *>(this->getElementById(id)))
        group->push_back(b);
    for (UiButton *b : *group)
      b->setRadioGroup(group);
  }

  // Tabs: action=3 buttons whose target is one of this layout's own
  // sub-layouts show that sub-layout while they are on
  std::function<void(UiElement *)> findTabs = [&](UiElement *e) {
    if (UiButton *b = dynamic_cast<UiButton *>(e)) {
      if (b->getActionType() == 3 && b->getActionTarget() != 0) {
        for (UiElement *c : this->children) {
          UiLayout *sub = dynamic_cast<UiLayout *>(c);
          if (sub && sub->getId() == b->getActionTarget())
            this->tabs.push_back({b, sub});
        }
      }
    }
    if (dynamic_cast<UiLayout *>(e))
      return; // a sub-layout's own buttons are its business
    for (UiElement *c : e->getChildren())
      findTabs(c);
  };
  for (UiElement *child : this->children)
    findTabs(child);
  this->syncTabs();

  // Pass 3a: fillers stretch between two named elements
  for (UiElement *child : this->children) {
    UiImage *img = dynamic_cast<UiImage *>(child);
    if (img && img->getFillerAnchor1() && img->getFillerAnchor2())
      img->setFillerAnchors(this->findById(img->getFillerAnchor1()),
                            this->findById(img->getFillerAnchor2()));
  }

  // Scrolling regions draw their items like their template button, which
  // is not shown itself; their scrollbar scrolls them
  std::function<void(UiElement *)> linkRegions = [&](UiElement *e) {
    if (UiScrollingRegion *region = dynamic_cast<UiScrollingRegion *>(e)) {
      if (UiButton *t = dynamic_cast<UiButton *>(
              this->getElementById(region->getTemplateId()))) {
        IniReader *r = this->ini_reader;
        std::string section;
        for (const std::string &s : r->getSections())
          if (r->getInt(s, "id", 0) == region->getTemplateId())
            section = s;
        region->setTemplate(t->getAnimationPath(),
                            r->getInt(section, "iconfirst", 1) != 0);
        t->setHidden(true);
      }
      if (UiScrollBar *bar = dynamic_cast<UiScrollBar *>(
              this->getElementById(region->getScrollBarId())))
        bar->attach(region);
    }
    if (dynamic_cast<UiLayout *>(e))
      return;
    for (UiElement *c : e->getChildren())
      linkRegions(c);
  };
  for (UiElement *child : this->children)
    linkRegions(child);

  // Pass 3: list boxes name the scrollbar that scrolls them
  // ("scrollbar=11505"); hand each scrollbar its list
  for (UiElement *child : this->children) {
    UiListBox *list = dynamic_cast<UiListBox *>(child);
    if (!list || list->getScrollBarId() == 0)
      continue;
    UiScrollBar *bar =
        dynamic_cast<UiScrollBar *>(this->getElementById(list->getScrollBarId()));
    if (bar)
      bar->attach(list);
  }
}

UiAction UiLayout::handleInputs(std::vector<Input> &inputs) {
  UiAction action = handleInputChildren(inputs);
  if (isPanelToggle(action)) {
    for (auto &tab : this->tabs) {
      if (tab.first->getActionTarget() == panelOf(action)) {
        this->syncTabs();
        return UiAction::NONE; // handled here
      }
    }
  }
  return action;
}

// Each tab's sub-layout is shown while a button for it is on
void UiLayout::syncTabs() {
  for (auto &tab : this->tabs) {
    bool on = false;
    for (auto &other : this->tabs)
      if (other.second == tab.second && other.first->isToggledOn())
        on = true;
    tab.second->setHidden(!on);
  }
}

// Find element by ID recursively
UiElement *UiLayout::getElementById(int targetId) {
  for (UiElement *child : this->children) {
    if (UiElement *found = child->findById(targetId))
      return found;
  }
  for (UiElement *child : this->children) {
    UiLayout *childLayout = dynamic_cast<UiLayout *>(child);
    if (childLayout) {
      UiElement *found = childLayout->getElementById(targetId);
      if (found)
        return found;
    }
  }
  return nullptr;
}
