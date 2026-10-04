#ifndef UI_SCROLLING_REGION_HPP
#define UI_SCROLLING_REGION_HPP

#include <functional>
#include <string>
#include <vector>

#include "UiElement.hpp"
#include "UiScrollBar.hpp"

class Animation;

// ============================================================================
// UIScrollingRegion: the buy panels' grid of item buttons
// ============================================================================
// Every item is drawn like the layout's template button (template=<id>, a
// 48x36 frame: N normal, H hover, S selected) with the item's icon inside
// it; iconfirst=1 draws the icon first, so the frame sits on top. The
// cells fill the region's width (autofit=1) with spacing=<x> and <y>
// between them, and scroll by rows with the region's scrollbar.
// ============================================================================
class UiScrollingRegion : public UiElement, public UiScrollable {
public:
  UiScrollingRegion(IniReader *ini_reader, ResourceManager *resource_manager,
                    std::string name);
  ~UiScrollingRegion();

  int getTemplateId() const { return this->template_id; }
  int getScrollBarId() const { return this->scrollbar_id; }
  // The frame every cell is drawn with (the template button's animation)
  void setTemplate(const std::string &animationPath, bool iconFirst);

  struct Item {
    std::string icon; // animation path
    std::string key;  // what the owner knows the item by
  };
  void setItems(const std::vector<Item> &items);
  // Shows another icon for one item (a rotated item's other facing)
  void setItemIcon(int index, const std::string &icon);
  int getSelected() const { return this->selected; }
  void setSelected(int index);
  // Called when the player picks an item
  std::function<void(int)> onSelect;

  UiAction handleInputs(std::vector<Input> &inputs) override;
  void draw(SDL_Renderer *renderer, SDL_Rect *layout_rect) override;

  // UiScrollable (rows)
  int getScrollPosition() const override { return this->first_row; }
  int getScrollMaximum() const override;
  int getScrollPage() const override { return this->visibleRows(); }
  void setScrollPosition(int position) override;
  // The scrollbar runs the height of the cells, not the region (measured:
  // the buy panels' 5 rows, the terraform page's 3)
  SDL_Rect getScrollBounds() const override;

private:
  int template_id = 0;
  int scrollbar_id = 0;
  int spacing_x = 0, spacing_y = 0;
  bool icon_first = true;
  Animation *frame = nullptr;
  int cell_w = 48, cell_h = 36;

  std::vector<Item> items;
  std::vector<Animation *> icons; // loaded as drawn
  std::vector<bool> iconTried;     // (an icon not in the data: a blank cell)
  int selected = -1;
  int hover = -1;
  int first_row = 0;

  int columns() const;
  int visibleRows() const;
  SDL_Rect cellRect(int index) const; // in the last drawn rect
};

#endif // UI_SCROLLING_REGION_HPP
