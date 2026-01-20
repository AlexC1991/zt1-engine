#ifndef UI_SCROLLBAR_HPP
#define UI_SCROLLBAR_HPP

#include "UiElement.hpp"
#include <SDL2/SDL.h>
#include <string>


class IniReader;
class ResourceManager;

class UiScrollBar : public UiElement {
public:
  UiScrollBar(IniReader *ini_reader, ResourceManager *resource_manager,
              std::string name);
  ~UiScrollBar();

  UiAction handleInputs(std::vector<Input> &inputs) override;
  void draw(SDL_Renderer *renderer, SDL_Rect *layout_rect) override;

private:
  int x = 0;
  int y = 0;
  int w = 20;
  int h = 100;

  int range = 100;
  int value = 0;
  int thumb_size = 20;
  bool horizontal = false;

  // Visuals
  SDL_Color back_color = {50, 50, 50, 255};
  SDL_Color thumb_color = {100, 100, 100, 255};

  SDL_Rect dest_rect = {0, 0, 0, 0};
};

#endif // UI_SCROLLBAR_HPP
