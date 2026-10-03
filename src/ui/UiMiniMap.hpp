#ifndef UI_MINIMAP_HPP
#define UI_MINIMAP_HPP

#include <functional>
#include <string>
#include <vector>

#include <SDL2/SDL.h>

#include "UiElement.hpp"

// The original's ZTMiniMap: the whole map drawn as a diamond in its box,
// plus a rectangle showing what is on screen. Clicking it moves the view.
// The game supplies the picture and handles clicks through callbacks, so
// the UI code doesn't depend on the world.
class UiMiniMap : public UiElement {
public:
  // Draw the map into the box (screen rect, in layout units)
  using DrawFn = std::function<void(SDL_Renderer *, const SDL_Rect &)>;
  // A click at fractions (0-1) across and down the box
  using ClickFn = std::function<void(float, float)>;

  UiMiniMap(IniReader *ini_reader, ResourceManager *resource_manager,
            std::string name);
  ~UiMiniMap();

  void setCallbacks(DrawFn draw, ClickFn click) {
    draw_fn = draw;
    click_fn = click;
  }

  UiAction handleInputs(std::vector<Input> &inputs) override;
  void draw(SDL_Renderer *renderer, SDL_Rect *layout_rect) override;

private:
  DrawFn draw_fn;
  ClickFn click_fn;
  SDL_Rect rect = {0, 0, 0, 0};
  bool dragging = false;
};

#endif // UI_MINIMAP_HPP
