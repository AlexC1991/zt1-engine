#ifndef UI_SCROLLBAR_HPP
#define UI_SCROLLBAR_HPP

#include "UiElement.hpp"
#include <SDL2/SDL.h>
#include <algorithm>
#include <functional>
#include <string>

class Animation;
class IniReader;
class ResourceManager;

// Something a scrollbar can scroll (e.g. a list box). Positions are in rows.
class UiScrollable {
public:
  virtual ~UiScrollable() = default;
  virtual int getScrollPosition() const = 0;
  virtual int getScrollMaximum() const = 0; // 0 = everything fits
  virtual int getScrollPage() const = 0;    // rows visible at once
  virtual void setScrollPosition(int position) = 0;
  // The scrollbar runs down the right-hand side of this rectangle
  virtual SDL_Rect getScrollBounds() const = 0;
};

// The original's UIScrollBar: an up arrow, a down arrow, a track stretched
// between them and a fixed-size thumb, all from ui/sharedui. It has no
// position of its own; the element that names it (e.g. a list box's
// "scrollbar=11505") places it along its right edge.
class UiScrollBar : public UiElement {
public:
  UiScrollBar(IniReader *ini_reader, ResourceManager *resource_manager,
              std::string name);
  ~UiScrollBar();

  void attach(UiScrollable *owner) { this->owner = owner; }

  // A scroll bar attached to no list is a value slider (the Game Options'
  // horizontal bars, horzslid/horzthm): its thumb along its own dx, a value
  // from 0 to 100 (zoo.exe's scroll bars hold theirs at +0x364), set by
  // dragging the thumb or clicking the bar. onChange is told each new value
  // (the original's <layout>_<bar>_click handlers)
  int getValue() const { return this->value; }
  void setValue(int v) { this->value = std::clamp(v, 0, 100); }
  std::function<void(int)> onChange;

  UiAction handleInputs(std::vector<Input> &inputs) override;
  void draw(SDL_Renderer *renderer, SDL_Rect *layout_rect) override;

private:
  UiScrollable *owner = nullptr;

  Animation *track = nullptr;
  Animation *up_arrow = nullptr;
  Animation *down_arrow = nullptr;
  Animation *thumb = nullptr;

  // Last drawn geometry, for hit testing
  SDL_Rect up_rect = {0, 0, 0, 0};
  SDL_Rect down_rect = {0, 0, 0, 0};
  SDL_Rect track_rect = {0, 0, 0, 0};
  SDL_Rect thumb_rect = {0, 0, 0, 0};

  bool hover_up = false;
  bool hover_down = false;
  bool dragging = false;
  int drag_offset = 0;

  bool visible() const;
  bool slider() const { return this->owner == nullptr; }
  int value = 0;
  void sliderTo(int mx);
  void setFromThumbTop(int thumbTop);
};

#endif // UI_SCROLLBAR_HPP
