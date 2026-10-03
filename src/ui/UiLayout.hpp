#ifndef UI_LAYOUT_HPP
#define UI_LAYOUT_HPP

#include <vector>

#include <SDL2/SDL.h>

#include "UiElement.hpp"

class UiButton;

#include "UiAction.hpp"

#include "../IniReader.hpp"
#include "../ResourceManager.hpp"
#include "../Input.hpp"

class UiLayout : public UiElement {
public:
  // A layout file (owns ini_reader)
  UiLayout(IniReader * ini_reader, ResourceManager * resource_manager);
  // A layout inside a layout: section `name` of ini_reader names the file
  // (layout=...); this layout loads and owns that file
  UiLayout(IniReader * ini_reader, ResourceManager * resource_manager, std::string name);
  ~UiLayout();

  UiAction handleInputs(std::vector<Input> &inputs);

  // The rect of the element this layout is anchored to (e.g. a panel's
  // anchor=1032, the HUD's toolbar column). Sub-layouts naming the same
  // anchor (the Game Options pages) are placed in it, not in this layout.
  void setAnchorRect(const SDL_Rect &rect) {
    this->anchor_rect = rect;
    this->has_anchor_rect = true;
  }
  UiElement* getElementById(int id);  // [PATCH] Find element by ID
  void draw(SDL_Renderer * renderer, SDL_Rect * layout_rect);

private:
  // Tabs: buttons (action=3 target=<id>) that show one of this layout's own
  // sub-layouts, e.g. the Game Options panel's pages
  std::vector<std::pair<UiButton *, UiLayout *>> tabs;
  void syncTabs();

  int layer_count = 0;
  bool nested = false; // placed by its own [LayoutInfo] inside its parent
  SDL_Rect anchor_rect = {0, 0, 0, 0};
  bool has_anchor_rect = false;
  SDL_Window * window = nullptr;

  void process_sections(IniReader * ini_reader, ResourceManager * resource_manager);
};

#endif // UI_LAYOUT_HPP