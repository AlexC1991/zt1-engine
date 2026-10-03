#include "UiScrollingRegion.hpp"

#include <algorithm>

#include "../Animation.hpp"
#include "../CompassDirection.hpp"
#include "../RenderSettings.hpp"

UiScrollingRegion::UiScrollingRegion(IniReader *ini_reader,
                                     ResourceManager *resource_manager,
                                     std::string name) {
  this->ini_reader = ini_reader;
  this->resource_manager = resource_manager;
  this->name = name;
  this->id = ini_reader->getInt(name, "id", 0);
  this->layer = ini_reader->getInt(name, "layer", 1);
  this->anchor = ini_reader->getInt(name, "anchor", 0);
  this->template_id = ini_reader->getInt(name, "template", 0);
  this->scrollbar_id = ini_reader->getInt(name, "scrollbar", 0);
  std::vector<std::string> spacing = ini_reader->getList(name, "spacing");
  if (spacing.size() > 0)
    this->spacing_x = std::atoi(spacing[0].c_str());
  if (spacing.size() > 1)
    this->spacing_y = std::atoi(spacing[1].c_str());
}

UiScrollingRegion::~UiScrollingRegion() {
  for (Animation *icon : this->icons)
    delete icon;
  delete this->frame;
  for (UiElement *child : this->children)
    delete child;
}

void UiScrollingRegion::setTemplate(const std::string &animationPath,
                                    bool iconFirst) {
  this->icon_first = iconFirst;
  delete this->frame;
  this->frame = animationPath.empty()
                    ? nullptr
                    : this->resource_manager->getAnimation(animationPath);
  if (this->frame) {
    int w = 0, h = 0;
    this->frame->queryTexture(CompassDirection::N, &w, &h);
    if (w > 0 && h > 0) {
      this->cell_w = w;
      this->cell_h = h;
    }
  }
}

void UiScrollingRegion::setItems(const std::vector<Item> &items) {
  for (Animation *icon : this->icons)
    delete icon;
  this->items = items;
  this->icons.assign(items.size(), nullptr);
  this->selected = -1;
  this->hover = -1;
  this->first_row = 0;
}

void UiScrollingRegion::setSelected(int index) {
  this->selected = (index >= 0 && index < (int)this->items.size()) ? index : -1;
}

int UiScrollingRegion::columns() const {
  int w = this->last_rect.w;
  return std::max(1, (w + this->spacing_x) / (this->cell_w + this->spacing_x));
}

int UiScrollingRegion::visibleRows() const {
  int h = this->last_rect.h;
  return std::max(1, (h + this->spacing_y) / (this->cell_h + this->spacing_y));
}

int UiScrollingRegion::getScrollMaximum() const {
  int cols = this->columns();
  int rows = ((int)this->items.size() + cols - 1) / cols;
  return std::max(0, rows - this->visibleRows());
}

void UiScrollingRegion::setScrollPosition(int position) {
  this->first_row = std::clamp(position, 0, this->getScrollMaximum());
}

// The grid is centred in the region (measured: the original's buy panels
// start their 2 x 5 cells 2 px in and 4 px down)
SDL_Rect UiScrollingRegion::cellRect(int index) const {
  int cols = this->columns();
  int rows = this->visibleRows();
  int usedW = cols * this->cell_w + (cols - 1) * this->spacing_x;
  int usedH = rows * this->cell_h + (rows - 1) * this->spacing_y;
  int row = index / cols - this->first_row;
  int col = index % cols;
  return {this->last_rect.x + (this->last_rect.w - usedW) / 2 +
              col * (this->cell_w + this->spacing_x),
          this->last_rect.y + (this->last_rect.h - usedH) / 2 +
              row * (this->cell_h + this->spacing_y),
          this->cell_w, this->cell_h};
}

UiAction UiScrollingRegion::handleInputs(std::vector<Input> &inputs) {
  for (Input &input : inputs) {
    if (input.type != InputType::POSITIONED)
      continue;
    SDL_Point p = {input.position.x, input.position.y};
    if (!SDL_PointInRect(&p, &this->last_rect)) {
      this->hover = -1;
      continue;
    }
    this->hover = -1;
    int cols = this->columns();
    int first = this->first_row * cols;
    int last = std::min((int)this->items.size(),
                        first + this->visibleRows() * cols);
    for (int i = first; i < last; i++) {
      SDL_Rect r = this->cellRect(i);
      if (SDL_PointInRect(&p, &r)) {
        this->hover = i;
        if (input.event == InputEvent::LEFT_CLICK && i != this->selected) {
          this->selected = i;
          if (this->onSelect)
            this->onSelect(i);
        }
        break;
      }
    }
  }
  return UiAction::NONE;
}

void UiScrollingRegion::draw(SDL_Renderer *renderer, SDL_Rect *layout_rect) {
  this->last_rect =
      this->getRect(this->ini_reader->getSection(this->name), layout_rect);
  if (!RenderSettings::drawsUiArt())
    return;

  int cols = this->columns();
  int first = this->first_row * cols;
  int last =
      std::min((int)this->items.size(), first + this->visibleRows() * cols);
  for (int i = first; i < last; i++) {
    SDL_Rect cell = this->cellRect(i);
    if (!this->icons[i] && !this->items[i].icon.empty())
      this->icons[i] = this->resource_manager->getAnimation(this->items[i].icon);

    auto drawIcon = [&]() {
      Animation *icon = this->icons[i];
      if (!icon)
        return;
      int w = 0, h = 0;
      icon->queryTexture(CompassDirection::N, &w, &h);
      SDL_Rect r = {cell.x + (cell.w - w) / 2, cell.y + (cell.h - h) / 2, w, h};
      icon->draw(renderer, &r, CompassDirection::N);
    };
    auto drawFrame = [&]() {
      if (!this->frame)
        return;
      CompassDirection state = CompassDirection::N;
      if (i == this->selected && this->frame->hasFrames(CompassDirection::S))
        state = CompassDirection::S;
      else if (i == this->hover && this->frame->hasFrames(CompassDirection::H))
        state = CompassDirection::H;
      SDL_Rect r = cell;
      this->frame->draw(renderer, &r, state);
    };
    if (this->icon_first) {
      drawIcon();
      drawFrame();
    } else {
      drawFrame();
      drawIcon();
    }
  }
}
