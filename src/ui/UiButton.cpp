#include "UiButton.hpp"

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
  // IMPORTANT: Do not SDL_DestroyTexture(text/shadow) here unless UiButton
  // *uniquely owns* them.
  //
  // In this project, getStringTexture(...) is very likely cached/owned by
  // ResourceManager, so destroying here can break other UI elements or cause
  // “text disappears after a second” issues.

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

  // (Re)build text textures when needed. Null-safe for icon-only buttons.
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

    // Do NOT destroy old textures here unless UiButton owns them.
    this->text = this->resource_manager->getStringTexture(
        renderer, this->font, this->text_string, color);
    this->shadow = this->resource_manager->getStringTexture(
        renderer, this->font, this->text_string, {0, 0, 0, 255});

    // We processed the update.
    this->selected_updated = false;
  }

  this->dest_rect =
      this->getRect(this->ini_reader->getSection(this->name), layout_rect);

  SDL_Rect text_rect = {dest_rect.x, dest_rect.y, 0, 0};

  // DEBUG: Force fallback to verify BMP rendering
  bool has_valid_animation = false;
  // this->animation != nullptr &&
  //                           this->animation->isValid() &&
  //                           this->animation->hasFrames(CompassDirection::N);

  if (has_valid_animation) {
    this->animation->draw(renderer, &dest_rect, CompassDirection::N);
  } else if (!this->transparent) {
    // Fallback: Try to use extracted BMPs first
    static SDL_Texture *s_texNormal = nullptr;
    static SDL_Texture *s_texSelected = nullptr;
    static bool s_triedLoading = false;
    static bool s_loggedFallback = false;

    if (!s_loggedFallback) {
      SDL_Log("UiButton: Entering FALLBACK logic for %s", this->name.c_str());
      s_loggedFallback = true;
    }

    if (!s_triedLoading && (!s_texNormal || !s_texSelected)) {
      auto applyPalette = [&](SDL_Surface *surf) {
        if (!surf || surf->format->BitsPerPixel != 8) {
          SDL_Log("UiButton: Surface is not 8-bit, skipping palette apply");
          return;
        }

        // HARDCODED FALLBACK PALETTE
        // Global palette is missing, and internal palette is wrong
        // (Red/Blue/Rainbow). We construct a Gold/Green palette based on index
        // analysis.
        SDL_Color colors[256];

        // TARGET COLORS: Gradient + Border (Safe Version)
        // User requested: "10% darker gradient... thin outter ring 30% darker"
        SDL_Color cBase = {75, 105, 45, 255};    // Base Army Green
        SDL_Color cBodyDark = {68, 95, 41, 255}; // 10% Darker
        SDL_Color cBorder = {53, 74, 32, 255};   // 30% Darker

        // 1. Initialize Transparency
        colors[0] = {0, 0, 0, 0};

        // 2. Initialize default (background/outliers) to BodyDark
        for (int i = 1; i < 256; ++i) {
          colors[i] = cBodyDark;
        }

        // 3. Apply Border (1-5) and Gradient (6-32)
        for (int i = 1; i < 33; ++i) {
          if (i <= 5) {
            colors[i] = cBorder;
          } else {
            float t = (float)(i - 6) / 26.0f;
            colors[i].r =
                (unsigned char)(cBase.r + (cBodyDark.r - cBase.r) * t);
            colors[i].g =
                (unsigned char)(cBase.g + (cBodyDark.g - cBase.g) * t);
            colors[i].b =
                (unsigned char)(cBase.b + (cBodyDark.b - cBase.b) * t);
            colors[i].a = 255;
          }
        }

        // by SDL_LoadBMP usually but we force opaque.
        // Force Opaque for all
        for (int i = 1; i < 256; ++i)
          colors[i].a = 255;

        SDL_SetPaletteColors(surf->format->palette, colors, 0, 256);
        SDL_SetColorKey(surf, SDL_TRUE, 0);
        SDL_Log("UiButton: Applied REFINED Gradient Palette");
      };

      // Try loading from current directory (where we extracted them)
      SDL_Surface *surfN = SDL_LoadBMP("button_background_N.bmp");
      if (surfN) {
        SDL_Log("UiButton: Loaded button_background_N.bmp SUCCESS (8-bit)");
        applyPalette(surfN);
        SDL_SetSurfaceBlendMode(surfN, SDL_BLENDMODE_BLEND);
        s_texNormal = SDL_CreateTextureFromSurface(renderer, surfN);
        SDL_FreeSurface(surfN);
      } else {
        SDL_Log("UiButton: Failed to load button_background_N.bmp: %s",
                SDL_GetError());
      }

      SDL_Surface *surfS = SDL_LoadBMP("button_background_S.bmp");
      if (surfS) {
        SDL_Log("UiButton: Loaded button_background_S.bmp SUCCESS (8-bit)");
        applyPalette(surfS);
        SDL_SetSurfaceBlendMode(surfS, SDL_BLENDMODE_BLEND);
        s_texSelected = SDL_CreateTextureFromSurface(renderer, surfS);
        SDL_FreeSurface(surfS);
      } else {
        SDL_Log("UiButton: Failed to load button_background_S.bmp: %s",
                SDL_GetError());
      }

      s_triedLoading = true;
    }

    // Fallback: Use texture dimensions if rect is empty (Match UiImage logic)
    SDL_Texture *tex = s_texNormal; // Just for size query
    if (tex && (dest_rect.w == 0 || dest_rect.h == 0)) {
      int tw, th;
      SDL_QueryTexture(tex, nullptr, nullptr, &tw, &th);
      dest_rect.w = tw;
      dest_rect.h = th;
    }

    // Old logic removed: text_rect.x += ... was causing drift/misalignment.
    // We will calculate exact center at the end.

    SDL_Texture *texToDraw =
        (this->selected && s_texSelected) ? s_texSelected : s_texNormal;

    if (texToDraw) {
      SDL_RenderCopy(renderer, texToDraw, nullptr, &dest_rect);

      // (Legacy logic removed - see end of function for centering)
    } else {
      // Final Fallback: Draw a simple styled button background (Gradient)

      // Draw outer border (dark)
      SDL_SetRenderDrawColor(renderer, 40, 50, 40, 255);
      SDL_RenderFillRect(renderer, &dest_rect);

      // Draw inner fill (gradient simulation - lighter middle)
      SDL_Rect inner = {dest_rect.x + 2, dest_rect.y + 2, dest_rect.w - 4,
                        dest_rect.h - 4};
      SDL_SetRenderDrawColor(renderer, 70, 85, 60, 255);
      SDL_RenderFillRect(renderer, &inner);

      // Draw highlight line at top
      SDL_Rect highlight = {dest_rect.x + 2, dest_rect.y + 2, dest_rect.w - 4,
                            2};
      SDL_SetRenderDrawColor(renderer, 100, 115, 85, 255);
      SDL_RenderFillRect(renderer, &highlight);

      // Draw shadow line at bottom
      SDL_Rect shadow_line = {dest_rect.x + 2, dest_rect.y + dest_rect.h - 4,
                              dest_rect.w - 4, 2};
      SDL_SetRenderDrawColor(renderer, 50, 60, 45, 255);
      SDL_RenderFillRect(renderer, &shadow_line);

      // (Legacy logic removed - see end of function for centering)
    }
  }

  if (this->text != nullptr) {
    int tw, th;
    SDL_QueryTexture(this->text, nullptr, nullptr, &tw, &th);
    text_rect.w = tw;
    text_rect.h = th;

    if (this->ini_reader->get(this->name, "justify") == "center") {
      // ROBUST CENTERING: Center of dest_rect minus half text size
      text_rect.x = dest_rect.x + (dest_rect.w - tw) / 2;
      // Adjust Y slightly up for visual balance (often text looks better
      // slightly high)
      text_rect.y = dest_rect.y + (dest_rect.h - th) / 2 - 1;
    } else {
      // Default top-left or whatever original relative pos was
      text_rect.x = dest_rect.x + 5; // Padding
      text_rect.y = dest_rect.y + 5;
    }
  }

  // Ensure hitbox exists even if animation/text missing.
  if (dest_rect.w == 0 || dest_rect.h == 0) {
    if (text_rect.w != 0 && text_rect.h != 0) {
      dest_rect = text_rect;
    } else {
      dest_rect.w = 1;
      dest_rect.h = 1;
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
    action = UiAction::NONE;
  }
  return action;
}