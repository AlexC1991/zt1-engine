#include "UiButton.hpp"
#include <set>

#include "../Animation.hpp"
#include "../CompassDirection.hpp"
#include "../IniReader.hpp"
#include "../ResourceManager.hpp"

UiButton::UiButton(IniReader *ini_reader, ResourceManager *resource_manager,
                   std::string name) {
  this->ini_reader = ini_reader;
  this->resource_manager = resource_manager;
  this->name = name;

  this->id = ini_reader->getInt(name, "id");
  this->layer = ini_reader->getInt(name, "layer", 1);
  this->anchor = ini_reader->getInt(name, "anchor", 0);
  this->transparent = ini_reader->getInt(name, "transparent", 0) != 0;

  this->has_select_color = !ini_reader->get(name, "selectcolor", "").empty();

  this->font = ini_reader->getInt(name, "font");

  uint32_t string_id = (uint32_t)ini_reader->getUnsignedInt(name, "textid");
  this->text_string = this->resource_manager->getString(string_id);

  std::string animation_path = ini_reader->get(name, "animation");
  if (!animation_path.empty()) {
    this->animation = resource_manager->getAnimation(animation_path);
  }
}

UiButton::~UiButton() {
  for (UiElement *child : this->children) {
    delete child;
  }
}

UiAction UiButton::handleInputs(std::vector<Input> &inputs) {
  UiAction action = UiAction::NONE;

  for (Input input : inputs) {
    if (input.type != InputType::POSITIONED) {
      continue;
    }

    if (input.position.x < this->dest_rect.x ||
        input.position.x > this->dest_rect.x + this->dest_rect.w) {
      this->selected_updated = true;
      this->selected = false;
      continue;
    }

    if (input.position.y < this->dest_rect.y ||
        input.position.y > this->dest_rect.y + this->dest_rect.h) {
      this->selected_updated = true;
      this->selected = false;
      continue;
    }

    this->selected = true;
    this->selected_updated = true;

    switch (input.event) {
    case InputEvent::LEFT_CLICK:
      if (this->ini_reader->getInt(this->name, "action", 0) == 1) {
        int target = this->ini_reader->getInt(this->name, "target", 0);
        if (target != 0) {
          action = (UiAction)target;
        }
      } else if (this->ini_reader->getInt(this->name, "action", 0) == 2) {
        action = UiAction::CREDITS_EXIT;
      } else {
        action = this->getActionBasedOnName();
      }
      break;

    default:
      break;
    }
  }

  handleInputChildren(inputs);
  return action;
}

void UiButton::draw(SDL_Renderer *renderer, SDL_Rect *layout_rect) {
  if (renderer == nullptr || layout_rect == nullptr) {
    return;
  }

  // (Re)build text textures when needed.
  if (!this->text_string.empty() &&
      (this->text == nullptr ||
       (this->selected_updated && this->has_select_color))) {
    std::vector<std::string> color_values;

    if (this->selected && this->has_select_color &&
        !ini_reader->getList(name, "selectcolor").empty()) {
      color_values = ini_reader->getList(name, "selectcolor");
    } else {
      color_values = ini_reader->getList(name, "forecolor");
    }

    SDL_Color color = {0, 0, 0, 255};
    if (color_values.size() == 3) {
      color = {
          (uint8_t)std::stoi(color_values[0]),
          (uint8_t)std::stoi(color_values[1]),
          (uint8_t)std::stoi(color_values[2]),
          255,
      };
    }

    this->text = this->resource_manager->getStringTexture(
        renderer, this->font, this->text_string, color);
    this->shadow = this->resource_manager->getStringTexture(
        renderer, this->font, this->text_string, {0, 0, 0, 255});

    this->selected_updated = false;
  }

  this->dest_rect =
      this->getRect(this->ini_reader->getSection(this->name), layout_rect);

  SDL_Rect text_rect = {dest_rect.x, dest_rect.y, 0, 0};

  // Check if we already have a valid animation from the INI
  bool has_valid_animation = this->animation != nullptr &&
                             this->animation->isValid() &&
                             this->animation->hasFrames(CompassDirection::N);

  bool is_nav_button =
      (this->name == "Back" || this->name == "Play" ||
       this->name == "Play Map" || this->name == "Back to main menu" ||
       this->name == "back" || this->name == "play" ||
       this->name == "back to main menu" || this->text_string == "Back" ||
       this->text_string == "Play" || this->text_string == "back" ||
       this->text_string == "play");

  // [PATCH 1] Lazy Load STATIC Assets (e.g. Back_N.png) if loose files exist
  // Skip spinner buttons - they are handled by PATCH 3
  bool is_spinner =
      (this->name == "up_spinner" || this->name == "down_spinner");

  // [PATCH] Suppress repeated log warnings for missing resources
  static std::set<std::string> known_missing;

  if (this->tex_normal == nullptr && !this->is_static && !is_spinner &&
      !known_missing.contains(this->name)) {
    this->tex_normal =
        this->resource_manager->getTexture(renderer, this->name + "_N");
    this->tex_hover =
        this->resource_manager->getTexture(renderer, this->name + "_H");
    this->tex_selected =
        this->resource_manager->getTexture(renderer, this->name + "_S");
    this->tex_disabled =
        this->resource_manager->getTexture(renderer, this->name + "_G");

    if (this->tex_normal) {
      this->is_static = true;
    } else {
      // If failed, mark as missing so we don't try (and log) again
      known_missing.insert(this->name);
    }
  }

  // [PATCH 2] Broken "Play" Button Fix
  // "Play" buttons don't have their own assets - use Back's static PNG instead.
  if (!has_valid_animation && !this->is_static) {
    if (this->name == "Play" || this->name == "play" ||
        this->name == "Play Map" || this->name == "startscenario") {

      // Load the Back button's static textures for this Play button
      this->tex_normal = this->resource_manager->getTexture(renderer, "Back_N");
      this->tex_hover = this->resource_manager->getTexture(renderer, "Back_H");

      if (this->tex_normal) {
        this->is_static = true;
      }
    }
  }

  // [PATCH 3] Spinner Button Fix (UNCONDITIONAL)
  // Force spinners to use static BMP textures instead of animation.
  // This runs regardless of whether animation exists.
  // NOTE: Runtime names are lowercase: "up_spinner" and "down_spinner"
  if (!this->is_static) {

    if (this->name == "up_spinner") {

      this->tex_normal =
          this->resource_manager->getTexture(renderer, "ui/sharedui/spinup_N");
      this->tex_hover =
          this->resource_manager->getTexture(renderer, "ui/sharedui/spinup_H");
      this->tex_selected =
          this->resource_manager->getTexture(renderer, "ui/sharedui/spinup_S");
      this->tex_disabled =
          this->resource_manager->getTexture(renderer, "ui/sharedui/spinup_G");

      if (this->tex_normal) {
        this->is_static = true;
        this->animation = nullptr; // Bypass animation rendering
        has_valid_animation = false;
      }
    } else if (this->name == "down_spinner") {

      this->tex_normal =
          this->resource_manager->getTexture(renderer, "ui/sharedui/spindwn_N");
      this->tex_hover =
          this->resource_manager->getTexture(renderer, "ui/sharedui/spindwn_H");
      this->tex_selected =
          this->resource_manager->getTexture(renderer, "ui/sharedui/spindwn_S");
      this->tex_disabled =
          this->resource_manager->getTexture(renderer, "ui/sharedui/spindwn_G");

      if (this->tex_normal) {
        this->is_static = true;
        this->animation = nullptr; // Bypass animation rendering
        has_valid_animation = false;
      }
    }
  }

  // DRAWING LOGIC

  if (this->is_static && is_nav_button) {
    // Defer drawing until size is calculated
  } else if (has_valid_animation) {
    this->animation->draw(renderer, &dest_rect, CompassDirection::N);
  } else if (this->is_static) {
    SDL_Texture *target = this->tex_normal;
    if (this->selected && this->tex_hover)
      target = this->tex_hover;

    // For spinners (and other static buttons with no size), get dimensions from
    // texture
    SDL_Rect render_rect = this->dest_rect;
    if ((render_rect.w <= 0 || render_rect.h <= 0) && target != nullptr) {
      int tw, th;
      SDL_QueryTexture(target, nullptr, nullptr, &tw, &th);
      render_rect.w = tw;
      render_rect.h = th;
      // Update the member dest_rect for hit testing
      this->dest_rect.w = tw;
      this->dest_rect.h = th;
    }
    SDL_RenderCopy(renderer, target, nullptr, &render_rect);
  }

  if (this->text != nullptr) {
    int tw, th;
    SDL_QueryTexture(this->text, nullptr, nullptr, &tw, &th);
    text_rect.w = tw;
    text_rect.h = th;

    if (this->ini_reader->get(this->name, "justify") == "center") {
      text_rect.x = dest_rect.x + (dest_rect.w - tw) / 2;
      text_rect.y = dest_rect.y + (dest_rect.h - th) / 2 - 1;
    } else {
      text_rect.x = dest_rect.x + 5;
      text_rect.y = dest_rect.y + 5;
    }
  }

  // --- AUTO-SIZING LOGIC ---
  if (dest_rect.w <= 1 || dest_rect.h <= 1) {
    if (text_rect.w != 0 && text_rect.h != 0) {

      // USER UI CONTROLS
      int offset_x = -4;
      int offset_y = -14;

      int padding_x = 5;
      int padding_y = 10;

      // REDUCED MIN SIZES
      int min_width = 130;
      int min_height = 55;

      if (is_nav_button) {
        dest_rect.x += offset_x;
        dest_rect.y += offset_y;

        dest_rect.w = (text_rect.w + padding_x > min_width)
                          ? text_rect.w + padding_x
                          : min_width;
        dest_rect.h = (text_rect.h + padding_y > min_height)
                          ? text_rect.h + padding_y
                          : min_height;
      } else {
        dest_rect.w =
            (text_rect.w + padding_x > 1) ? text_rect.w + padding_x : 1;
        dest_rect.h =
            (text_rect.h + padding_y > 1) ? text_rect.h + padding_y : 1;
      }

      // Re-Center Text
      text_rect.x = dest_rect.x + (dest_rect.w - text_rect.w) / 2;
      text_rect.y = dest_rect.y + (dest_rect.h - text_rect.h) / 2;
    } else {
      dest_rect.w = 1;
      dest_rect.h = 1;
    }
  }

  // Draw Static Nav Button (now that size is set)
  if (this->is_static) {
    SDL_Texture *t = this->tex_normal;
    if (this->selected && this->tex_hover) {
      t = this->tex_hover;
    }
    if (t) {
      SDL_RenderCopy(renderer, t, NULL, &dest_rect);
    }
  }

  // Fallback Gradient (Only if NO animation and NO image found)
  if (!has_valid_animation && !this->is_static) {
    bool force_bg =
        (this->name == "Back" || this->name == "Play" || this->name == "back" ||
         this->name == "play" || this->name == "back to main menu" ||
         this->text_string == "Back" || this->text_string == "Play" ||
         this->text_string == "back" || this->text_string == "play");

    if (!this->transparent || force_bg) {
      SDL_SetRenderDrawColor(renderer, 40, 50, 40, 255);
      SDL_RenderFillRect(renderer, &dest_rect);

      SDL_Rect inner = {dest_rect.x + 2, dest_rect.y + 2, dest_rect.w - 4,
                        dest_rect.h - 4};
      SDL_SetRenderDrawColor(renderer, 70, 85, 60, 255);
      SDL_RenderFillRect(renderer, &inner);

      SDL_Rect highlight = {dest_rect.x + 2, dest_rect.y + 2, dest_rect.w - 4,
                            2};
      SDL_SetRenderDrawColor(renderer, 100, 115, 85, 255);
      SDL_RenderFillRect(renderer, &highlight);

      SDL_Rect shadow_line = {dest_rect.x + 2, dest_rect.y + dest_rect.h - 4,
                              dest_rect.w - 4, 2};
      SDL_SetRenderDrawColor(renderer, 50, 60, 45, 255);
      SDL_RenderFillRect(renderer, &shadow_line);
    }
  }

  if (this->shadow != nullptr && text_rect.w > 0 && text_rect.h > 0) {
    shadow_rect = {text_rect.x - 1, text_rect.y + 1, text_rect.w, text_rect.h};
    SDL_RenderCopy(renderer, this->shadow, nullptr, &shadow_rect);
  }

  if (this->text != nullptr && text_rect.w > 0 && text_rect.h > 0) {
    SDL_RenderCopy(renderer, this->text, nullptr, &text_rect);
  }

  this->drawChildren(renderer, &dest_rect);
}

UiAction UiButton::getActionBasedOnName() {
  UiAction action = UiAction::NONE;
  if (this->name == "exit") {
    action = UiAction::STARTUP_EXIT;
  } else if (this->name == "back to main menu") {
    action = UiAction::SCENARIO_BACK_TO_MAIN_MENU;
  } else {
    if (this->id == 11511 || this->id == 11512) {
      action = static_cast<UiAction>(this->id);
    }
  }
  return action;
}