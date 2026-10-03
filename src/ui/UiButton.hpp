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

  // Toggle buttons: layout state 2048 (e.g. the HUD's tree/guest/building
  // view toggles, which start on, and the toolbar's panel buttons) or 4096
  // (a choice in a radio set, e.g. a panel's tabs)
  bool isToggle() const { return (this->state_flags & (2048 | 4096)) != 0; }
  bool isToggledOn() const { return this->toggled_on; }
  void setToggledOn(bool on) { this->toggled_on = on; }

  // Buttons of a UIRadioSet: turning one on turns the others off. They
  // start off unless the layout selects one (state 8, e.g. a panel's first
  // tab); the HUD's rating and panel buttons start off.
  void setRadioGroup(std::shared_ptr<std::vector<UiButton *>> group) {
    this->radio_group = group;
    this->toggled_on = (this->state_flags & 8) != 0;
  }

  // The layout's animation= and stringData= (e.g. a tab's category)
  std::string getAnimationPath() const {
    return this->ini_reader->get(this->name, "animation");
  }
  std::string getStringData() const {
    return this->ini_reader->get(this->name, "stringdata");
  }

  // The first helpid= (a tab's help string is also its panel's title)
  int getHelpId() const {
    std::vector<std::string> ids = this->ini_reader->getList(this->name, "helpid");
    return ids.empty() ? 0 : std::atoi(ids[0].c_str());
  }
  // Text shown on the button, set by the game (e.g. a filter's "All")
  void setLabel(const std::string &label) {
    this->text_string = label;
    this->text = nullptr;
  }

  // The layout's action=/target= (3 = toggle panel <target>, 2 = close)
  int getActionType() const { return this->action_type; }
  int getActionTarget() const { return this->action_target; }

private:
  int action_type = 0;
  int action_target = 0;
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