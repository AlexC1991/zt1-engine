#ifndef UI_BUTTON_HPP
#define UI_BUTTON_HPP

#include <string>

#include <SDL2/SDL.h>

#include "UiElement.hpp"

// Forward declarations to avoid include cycles.
class IniReader;
class ResourceManager;
class Animation;

class UiButton : public UiElement {
public:
  UiButton(IniReader *ini_reader, ResourceManager *resource_manager,
           std::string name);
  ~UiButton();

  UiAction handleInputs(std::vector<Input> &inputs);
  void draw(SDL_Renderer *renderer, SDL_Rect *layout_rect);

private:
  std::string text_string = "";
  SDL_Texture *text = nullptr;
  SDL_Texture *shadow = nullptr;
  int font = 0;
  Animation *animation = nullptr;
  
  // Static Image Fallback
  SDL_Texture *tex_normal = nullptr;
  SDL_Texture *tex_hover = nullptr;
  SDL_Texture *tex_selected = nullptr;
  SDL_Texture *tex_disabled = nullptr;
  bool is_static = false;
  bool selected = false;
  bool selected_updated = false;
  bool has_select_color = false;
  bool transparent = false;
  SDL_Rect dest_rect = {0, 0, 0, 0};
  SDL_Rect shadow_rect = {0, 0, 0, 0};

  UiAction getActionBasedOnName();
};

#endif // UI_BUTTON_HPP