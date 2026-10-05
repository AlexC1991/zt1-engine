#include <cmath>
#include "UiMiniMap.hpp"

#include <algorithm>

UiMiniMap::UiMiniMap(IniReader *ini_reader, ResourceManager *resource_manager,
                     std::string name) {
  this->ini_reader = ini_reader;
  this->resource_manager = resource_manager;
  this->name = name;
  this->id = ini_reader->getInt(name, "id", 0);
  this->layer = ini_reader->getInt(name, "layer", 1);
  this->anchor = ini_reader->getInt(name, "anchor", 0);
}

UiMiniMap::~UiMiniMap() {
  for (UiElement *child : this->children)
    delete child;
}

UiAction UiMiniMap::handleInputs(std::vector<Input> &inputs) {
  for (const Input &input : inputs) {
    if (input.type != InputType::POSITIONED)
      continue;
    // (the map's diamond, not its box: the view toggles sit in the box's
    // corner and take their own clicks)
    bool inside = false;
    if (rect.w > 0 && rect.h > 0) {
      float ux = (input.x - rect.x) / float(rect.w) - 0.5f, uy = (input.y - rect.y) / float(rect.h) - 0.5f;
      inside = std::fabs(ux) + std::fabs(uy) <= 0.5f;
    }
    if (input.event == InputEvent::LEFT_CLICK && inside)
      dragging = true;
    if (dragging && !(SDL_GetMouseState(nullptr, nullptr) & SDL_BUTTON_LMASK) &&
        input.event != InputEvent::LEFT_CLICK)
      dragging = false;
    if (dragging && click_fn && rect.w > 0 && rect.h > 0 &&
        (input.event == InputEvent::LEFT_CLICK ||
         input.event == InputEvent::CURSOR_MOVE)) {
      float fx = (input.x - rect.x) / float(rect.w);
      float fy = (input.y - rect.y) / float(rect.h);
      click_fn(std::clamp(fx, 0.0f, 1.0f), std::clamp(fy, 0.0f, 1.0f));
    }
  }
  return handleInputChildren(inputs);
}

void UiMiniMap::draw(SDL_Renderer *renderer, SDL_Rect *layout_rect) {
  if (!renderer || !layout_rect)
    return;
  rect = this->getRect(this->ini_reader->getSection(this->name), layout_rect);
  this->last_rect = rect;
  if (draw_fn)
    draw_fn(renderer, rect);
  this->drawChildren(renderer, &rect);
}
