#ifndef UI_BUTTON_HPP
#define UI_BUTTON_HPP

#include <memory>
#include <string>
#include <vector>

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

  // Toggle buttons (layout state 2048, e.g. the HUD's tree/guest/building
  // view toggles) start on and flip on each click
  bool isToggle() const { return (this->state_flags & 2048) != 0; }
  bool isToggledOn() const { return this->toggled_on; }

  // Buttons of a UIRadioSet: toggles in a set start off (the HUD's rating
  // buttons, which open their panels), and turning one on turns the others
  // off
  void setRadioGroup(std::shared_ptr<std::vector<UiButton *>> group) {
    this->radio_group = group;
    this->toggled_on = false;
  }

private:
  bool toggled_on = true;
  std::shared_ptr<std::vector<UiButton *>> radio_group;
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