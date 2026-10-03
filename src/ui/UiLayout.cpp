#include "UiLayout.hpp"

#include <algorithm>
#include <cstdlib>
#include <functional>

#include "UiButton.hpp"
#include "UiImage.hpp"
#include "UiListBox.hpp"
#include "UiMiniMap.hpp"
#include "UiStatusImage.hpp"
#include "UiScrollBar.hpp"
#include "UiText.hpp"

UiLayout::UiLayout(IniReader *ini_reader, ResourceManager *resource_manager) {
  this->ini_reader = ini_reader;
  this->resource_manager = resource_manager;

  this->name = "layoutinfo";
  this->id = ini_reader->getInt(this->name, "id", 0);
  this->layer = ini_reader->getInt(this->name, "layer", 1);

  this->process_sections(ini_reader, resource_manager);
}

UiLayout::UiLayout(IniReader *ini_reader, ResourceManager *resource_manager,
                   std::string name) {
  this->ini_reader = ini_reader;
  this->resource_manager = resource_manager;

  this->name = name;
  this->id = ini_reader->getInt(name, "id", 0);
  this->layer = ini_reader->getInt(name, "layer", 1);
  this->anchor = ini_reader->getInt(name, "anchor", 0);

  this->process_layout(resource_manager, ini_reader->get(name, "layout"));
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

  if (layout_rect == nullptr) {
    if (!window) {
      this->window = SDL_RenderGetWindow(renderer);
    }
    SDL_Rect window_rect = {0, 0, 0, 0};
    SDL_GetWindowSize(this->window, &window_rect.w, &window_rect.h);
    layout_rect = &window_rect;
  }

  // Like the original: every element drawn in layer order across the whole
  // layout (not parent-then-children), each positioned inside its anchor
  struct Item {
    UiElement *element;
    SDL_Rect parent;
  };
  std::vector<Item> items;
  std::function<void(UiElement *, SDL_Rect)> visit = [&](UiElement *e,
                                                          SDL_Rect parent) {
    if (e->isHidden())
      return;
    items.push_back({e, parent});
    if (dynamic_cast<UiLayout *>(e))
      return;
    SDL_Rect r = e->computeRect(renderer, &parent);
    for (UiElement *c : e->getChildren())
      visit(c, r);
  };
  for (UiElement *child : this->children)
    visit(child, *layout_rect);

  int maxLayer = 0;
  for (const Item &it : items)
    maxLayer = std::max(maxLayer, it.element->getLayer());
  for (int layer = 0; layer <= maxLayer; layer++) {
    for (Item &it : items) {
      if (it.element->getLayer() == layer)
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
      // Groups toolbar buttons so only one is selected; no visuals
      continue;
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

  // Pass 3a: fillers stretch between two named elements
  for (UiElement *child : this->children) {
    UiImage *img = dynamic_cast<UiImage *>(child);
    if (img && img->getFillerAnchor1() && img->getFillerAnchor2())
      img->setFillerAnchors(this->findById(img->getFillerAnchor1()),
                            this->findById(img->getFillerAnchor2()));
  }

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

void UiLayout::process_layout(ResourceManager *resource_manager,
                              std::string layout) {
  if (layout.empty()) {
    return;
  }

  IniReader *child_reader = resource_manager->getIniReader(layout);
  process_sections(child_reader, resource_manager);

  // We created child_reader with new; process_sections does not take ownership.
  delete child_reader;
}

UiAction UiLayout::handleInputs(std::vector<Input> &inputs) {
  return handleInputChildren(inputs);
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
