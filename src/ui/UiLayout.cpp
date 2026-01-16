#include "UiLayout.hpp"

#include "UiButton.hpp"
#include "UiImage.hpp"
#include "UiListBox.hpp"
#include "UiText.hpp"

UiLayout::UiLayout(IniReader *ini_reader, ResourceManager *resource_manager) {
  this->ini_reader = ini_reader;
  this->resource_manager = resource_manager;

  this->name = "layoutinfo";
  this->id = ini_reader->getInt(this->name, "id", 0);
  this->layer = ini_reader->getInt(this->name, "layer", 1);

  this->process_sections(ini_reader, resource_manager);
}

UiLayout::UiLayout(
  IniReader *ini_reader,
  ResourceManager *resource_manager,
  std::string name
) {
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
  if (renderer == nullptr) return;

  if (layout_rect == nullptr) {
    if (!window) {
      this->window = SDL_RenderGetWindow(renderer);
    }
    SDL_Rect window_rect = {0, 0, 0, 0};
    SDL_GetWindowSize(this->window, &window_rect.w, &window_rect.h);
    layout_rect = &window_rect;
  }

  drawChildren(renderer, layout_rect);
}

void UiLayout::process_sections(
  IniReader *ini_reader,
  ResourceManager *resource_manager
) {
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

    // Map preview boxes sometimes have odd metadata; force UiImage for known names.
    if (section == "smap" || section == "fmap" || section == "map_preview") {
      new_element =
        (UiElement *)new UiImage(ini_reader, resource_manager, section);
    } else if (element_type == "UIImage") {
      new_element =
        (UiElement *)new UiImage(ini_reader, resource_manager, section);
    } else if (element_type == "UIButton") {
      new_element =
        (UiElement *)new UiButton(ini_reader, resource_manager, section);
    } else if (element_type == "UIText") {
      new_element =
        (UiElement *)new UiText(ini_reader, resource_manager, section);
    } else if (element_type == "UIListBox") {
      new_element =
        (UiElement *)new UiListBox(ini_reader, resource_manager, section);
    } else if (element_type == "UILayout") {
      new_element =
        (UiElement *)new UiLayout(ini_reader, resource_manager, section);
    } else {
      if (element_type.empty()) {
        SDL_Log("Could not determine type of section %s", section.c_str());
      } else {
        SDL_Log(
          "Unknown UI element type '%s' in section %s",
          element_type.c_str(),
          section.c_str()
        );
      }
    }

    if (!new_element) continue;

    int anchorId = new_element->getAnchor();
    if (anchorId == 0 || anchorId == this->id) {
      this->children.push_back(new_element);
    } else {
      pending_anchored.push_back(new_element);
    }
  }

  // Pass 2: attach anchored elements using recursive lookup.
  for (UiElement *child : pending_anchored) {
    int anchorId = child->getAnchor();
    UiElement *anchorTarget = this->getElementById(anchorId);

    if (anchorTarget != nullptr) {
      anchorTarget->addChild(child);
    } else {
      SDL_Log(
        "Anchor id %d was not found for element id=%d name='%s'",
        anchorId,
        child->getId(),
        child->getName().c_str()
      );

      // Fallback: attach to root so it still exists (helps debugging and
      // avoids “missing UI element” issues).
      this->children.push_back(child);
    }
  }
}

void UiLayout::process_layout(ResourceManager *resource_manager, std::string layout) {
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
    if (child->getId() == targetId) {
      return child;
    }
  }

  for (UiElement *child : this->children) {
    UiLayout *childLayout = dynamic_cast<UiLayout *>(child);
    if (childLayout) {
      UiElement *found = childLayout->getElementById(targetId);
      if (found) return found;
    }
  }

  return nullptr;
}