#include "UiScrollBar.hpp"
#include "../Animation.hpp"
#include "../CompassDirection.hpp"
#include "../IniReader.hpp"
#include "../ResourceManager.hpp"
#include <SDL2/SDL.h>
#include <algorithm>

namespace {

Animation *loadPart(IniReader *ini, ResourceManager *rm, const std::string &name,
                    const char *key) {
  std::string path = ini->get(name, key);
  return path.empty() ? nullptr : rm->getAnimation(path);
}

void partSize(Animation *a, int *w, int *h) {
  *w = *h = 0;
  if (a)
    a->queryTexture(CompassDirection::N, w, h);
}

bool inside(const SDL_Rect &r, int x, int y) {
  return x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h;
}

} // namespace

UiScrollBar::UiScrollBar(IniReader *ini_reader,
                         ResourceManager *resource_manager, std::string name) {
  this->name = name;
  this->ini_reader = ini_reader;
  this->resource_manager = resource_manager;
  this->id = ini_reader->getInt(name, "id", 0);
  this->layer = ini_reader->getInt(name, "layer", 0);
  this->anchor = ini_reader->getInt(name, "anchor", 0);

  this->track = loadPart(ini_reader, resource_manager, name, "animation");
  this->up_arrow = loadPart(ini_reader, resource_manager, name, "upArrow");
  this->down_arrow = loadPart(ini_reader, resource_manager, name, "downArrow");
  this->thumb = loadPart(ini_reader, resource_manager, name, "thumb");
}

UiScrollBar::~UiScrollBar() {}

bool UiScrollBar::visible() const {
  return owner != nullptr && owner->getScrollMaximum() > 0;
}

void UiScrollBar::setFromThumbTop(int thumbTop) {
  int travel = track_rect.h - thumb_rect.h;
  int maximum = owner->getScrollMaximum();
  if (travel <= 0 || maximum <= 0)
    return;
  int offset = std::clamp(thumbTop - track_rect.y, 0, travel);
  owner->setScrollPosition((offset * maximum + travel / 2) / travel);
}

UiAction UiScrollBar::handleInputs(std::vector<Input> &inputs) {
  if (!visible())
    return UiAction::NONE;

  for (const Input &input : inputs) {
    if (input.type != InputType::POSITIONED)
      continue;
    int mx = input.position.x, my = input.position.y;

    if (input.event == InputEvent::CURSOR_MOVE) {
      hover_up = inside(up_rect, mx, my);
      hover_down = inside(down_rect, mx, my);
      if (dragging) {
        if (SDL_GetMouseState(nullptr, nullptr) & SDL_BUTTON_LMASK)
          setFromThumbTop(my - drag_offset);
        else
          dragging = false;
      }
    } else if (input.event == InputEvent::LEFT_CLICK) {
      int position = owner->getScrollPosition();
      if (inside(up_rect, mx, my)) {
        owner->setScrollPosition(position - 1);
      } else if (inside(down_rect, mx, my)) {
        owner->setScrollPosition(position + 1);
      } else if (inside(thumb_rect, mx, my)) {
        dragging = true;
        drag_offset = my - thumb_rect.y;
      } else if (inside(track_rect, mx, my)) {
        // Page towards the click
        int page = std::max(1, owner->getScrollPage() - 1);
        owner->setScrollPosition(my < thumb_rect.y ? position - page
                                                   : position + page);
      }
    }
  }
  return UiAction::NONE;
}

void UiScrollBar::draw(SDL_Renderer *renderer, SDL_Rect *layout_rect) {
  (void)layout_rect;
  if (!renderer || !visible())
    return;

  int upW, upH, downW, downH, thumbW, thumbH;
  partSize(up_arrow, &upW, &upH);
  partSize(down_arrow, &downW, &downH);
  partSize(thumb, &thumbW, &thumbH);
  int width = std::max({upW, downW, thumbW, 12});

  // Along the owner's right edge, its full height
  SDL_Rect bounds = owner->getScrollBounds();
  int x = bounds.x + bounds.w;
  up_rect = {x, bounds.y, width, upH};
  down_rect = {x, bounds.y + bounds.h - downH, width, downH};
  track_rect = {x, up_rect.y + up_rect.h, width,
                down_rect.y - (up_rect.y + up_rect.h)};

  int travel = std::max(0, track_rect.h - thumbH);
  int maximum = owner->getScrollMaximum();
  int position = std::clamp(owner->getScrollPosition(), 0, maximum);
  int thumbY = track_rect.y + (maximum > 0 ? travel * position / maximum : 0);
  thumb_rect = {x, thumbY, width, thumbH};

  if (track)
    track->draw(renderer, &track_rect, CompassDirection::N);
  if (thumb)
    thumb->draw(renderer, &thumb_rect, CompassDirection::N);
  // Arrow art has N (normal) and H (highlighted) states
  if (up_arrow)
    up_arrow->draw(renderer, &up_rect,
                   hover_up ? CompassDirection::H : CompassDirection::N);
  if (down_arrow)
    down_arrow->draw(renderer, &down_rect,
                     hover_down ? CompassDirection::H : CompassDirection::N);
}
