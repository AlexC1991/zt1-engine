#include "UiText.hpp"
#include "../RenderSettings.hpp"
#include <sstream>
#include <vector>
#include <algorithm>
#include <cmath>
#include <map>
#include "../Input.hpp" 

// [STATE MANAGER]
static std::map<UiText*, int> scroll_y_map;
static UiText *s_editing = nullptr; // the box being edited

static int getScroll(UiText* ptr) { return scroll_y_map[ptr]; }
static void setScroll(UiText* ptr, int v) { scroll_y_map[ptr] = v; }

UiText::UiText(IniReader * ini_reader, ResourceManager * resource_manager, std::string name) {
  this->ini_reader = ini_reader;
  this->resource_manager = resource_manager;
  this->name = name;
  this->id = ini_reader->getInt(name, "id");
  this->font = ini_reader->getInt(name, "font");
  this->anchor = ini_reader->getInt(name, "anchor", 0);
  this->layer = ini_reader->getInt(name, "layer", 1);

  // Initial text: the layout's textid= string. Text without one (a name,
  // a price, the date) is filled in by the game.
  int string_id = ini_reader->getInt(name, "textid", 0);
  std::string raw =
      string_id > 0 ? this->resource_manager->getString(string_id) : "";
  if(raw.empty()) raw = (name == "version_label") ? "Version: ZT1-Engine 0.1" : "";
  
  this->setText(raw);

  this->editable = ini_reader->get(name, "type") == "UIEditableText";
  this->numeric = ini_reader->getInt(name, "numeric", 0) == 1;
  this->char_limit = ini_reader->getInt(name, "charlimit", 0);
}

bool UiText::isEditing() { return s_editing != nullptr; }
bool UiText::isBeingEdited() const { return s_editing == this; }

void UiText::beginEdit() {
  if (s_editing == this)
    return;
  if (s_editing)
    s_editing->endEdit(true);
  s_editing = this;
  this->before_edit = this->text_string;
  if (this->onBeginEdit)
    this->setText(this->onBeginEdit());
  SDL_StartTextInput();
}

void UiText::endEdit(bool keep) {
  if (s_editing != this)
    return;
  s_editing = nullptr;
  SDL_StopTextInput();
  std::string typed = this->text_string;
  this->setText(this->before_edit);
  if (keep && this->onCommit)
    this->onCommit(typed);
}

UiText::~UiText() {
  if (s_editing == this)
    s_editing = nullptr;
  scroll_y_map.erase(this);
  if (text) SDL_DestroyTexture(text);
}

// [HELPER] Word Wrap - PARAGRAPH AWARE
static std::vector<std::string> splitToLines(std::string text, size_t max_chars) {
    std::vector<std::string> lines;
    std::stringstream ss(text);
    std::string segment;

    // 1. Split by Newline First (Respects explicit line breaks)
    while (std::getline(ss, segment, '\n')) {
        
        // FIX: If the segment is empty, it means we hit a blank line (\n\n)
        // We must push an empty string to force a visual line break.
        if (segment.empty()) {
            lines.push_back(""); 
            continue;
        }

        // 2. Wrap words WITHIN this paragraph
        std::istringstream words(segment);
        std::string word;
        std::string current_line;
        
        while (words >> word) {
            if (current_line.length() + word.length() + 1 > max_chars) {
                lines.push_back(current_line);
                current_line = word;
            } else {
                if (!current_line.empty()) current_line += " ";
                current_line += word;
            }
        }
        if (!current_line.empty()) lines.push_back(current_line);
    }
    return lines;
}

void UiText::setText(const std::string& newText) {
  if (text_string != newText) {
    text_string = newText;
    // 55 chars fits the box well
    this->cached_lines = splitToLines(newText, 55); 
    setScroll(this, 0); 
  }
}

void UiText::setMessage(const std::string& newText) {
  text_string = newText;
  this->cached_lines = {newText};
  setScroll(this, 0);
}

// [INPUT] Mouse Wheel Support
UiAction UiText::handleInputs(std::vector<Input> &inputs) {
  if (this->editable) {
    for (const Input &input : inputs) {
      if (input.event == InputEvent::LEFT_CLICK) {
        SDL_Point p = {input.x, input.y};
        if (SDL_PointInRect(&p, &this->last_rect))
          this->beginEdit();
        else if (s_editing == this)
          this->endEdit(true);
      } else if (s_editing != this) {
        continue;

      } else if (input.event == InputEvent::TEXT_INPUT) {
        std::string add;
        for (const char *c = input.text; *c; c++)
          if (!this->numeric || (*c >= '0' && *c <= '9') || *c == '.')
            add += *c;
        std::string next = this->text_string + add;
        if (this->char_limit <= 0 || (int)next.size() <= this->char_limit)
          this->setText(next);
      } else if (input.event == InputEvent::KEY_DOWN) {
        if (input.key == SDLK_BACKSPACE && !this->text_string.empty()) {
          std::string s = this->text_string;
          s.pop_back();
          while (!s.empty() && (s.back() & 0xC0) == 0x80) // whole UTF-8 chars
            s.pop_back();
          this->setText(s);
        } else if (input.key == SDLK_RETURN || input.key == SDLK_KP_ENTER) {
          this->endEdit(true);
        }
      } else if (input.event == InputEvent::KEY_ESCAPE) {
        this->endEdit(false);
      }
    }
    return UiAction::NONE;
  }
  if (cached_lines.size() <= 1) return UiAction::NONE; // Don't scroll single lines

  // [FIX] Add +15 pixels buffer to account for padding
  int total_h = ((int)cached_lines.size() * 14) + 15; 
  int view_h = dest_rect.h;
  int max_scroll = std::max(0, total_h - view_h);
  
  if (max_scroll == 0) return UiAction::NONE; // Nothing to scroll

  int current = getScroll(this);

  for (const auto& input : inputs) {
      if (input.event == InputEvent::SCROLL_UP) {
          SDL_Point p = {input.x, input.y};
          if (SDL_PointInRect(&p, &dest_rect)) {
              current -= 20;
              if (current < 0) current = 0;
              setScroll(this, current);
              return UiAction::NONE;
          }
      }
      else if (input.event == InputEvent::SCROLL_DOWN) {
          SDL_Point p = {input.x, input.y};
          if (SDL_PointInRect(&p, &dest_rect)) {
              current += 20;
              if (current > max_scroll) current = max_scroll;
              setScroll(this, current);
              return UiAction::NONE;
          }
      }
  }
  return UiAction::NONE;
}

void UiText::draw(SDL_Renderer * renderer, SDL_Rect * layout_rect) {
  // Being edited: the text with a blinking caret after it
  if (s_editing == this) {
    std::vector<std::string> saved = this->cached_lines;
    this->cached_lines = {this->text_string +
                          ((SDL_GetTicks() / 500) % 2 ? " " : "|")};
    this->drawLines(renderer, layout_rect);
    this->cached_lines = saved;
    return;
  }
  this->drawLines(renderer, layout_rect);
}

void UiText::drawLines(SDL_Renderer * renderer, SDL_Rect * layout_rect) {
  if (this->cached_lines.empty()) return;

  // 1. Calculate Container
  dest_rect = this->getRect(this->ini_reader->getSection(this->name), layout_rect);

  // getRect shifts justified elements by their width (what buttons need);
  // text justifies itself inside its box below, so undo that shift. Without
  // this, centred text sits half a box too far right and right-justified
  // text (Starting Cash) a whole box, behind the Back button.
  const std::string justify = this->ini_reader->get(this->name, "justify");
  if (justify == "center")
    dest_rect.x -= dest_rect.w / 2;
  else if (justify == "right")
    dest_rect.x -= dest_rect.w;
  
  // border: the text sits that far inside its box on every side (the HUD's
  // date and money use 2; measured 2 px lower than without it)
  int border = this->ini_reader->getInt(this->name, "border", 0);
  if (border > 0) {
    dest_rect.x += border;
    dest_rect.y += border;
    dest_rect.w -= 2 * border;
    dest_rect.h -= 2 * border;
  }
  // Where it is, for getLastRect and clicks: a fitfont box is a line tall
  this->last_rect = dest_rect;
  if (this->last_rect.h <= 0)
    this->last_rect.h = resource_manager->getFontLineHeight(
        font, ini_reader->getInt(name, "fontsize", 0));

  bool is_multiline = (this->cached_lines.size() > 1);

  // One line of text wider than its box wraps at word breaks to the box's
  // width, each line justified like the text (e.g. the Staff panel's
  // duties). Long texts keep the scrolling box below.
  if (!is_multiline && dest_rect.w > 0) {
    const int fontSize = ini_reader->getInt(name, "fontsize", 0);
    auto width = [&](const std::string &str) {
      SDL_Texture *t = resource_manager->getStringTexture(
          renderer, font, str, SDL_Color{255, 255, 255, 255}, fontSize);
      int w = 0, h = 0;
      if (t)
        resource_manager->getTextSize(t, &w, &h);
      return w;
    };
    const std::string &text = this->cached_lines.front();
    if (width(text) > dest_rect.w) {
      std::vector<std::string> lines;
      std::string current;
      std::istringstream words(text);
      std::string word;
      while (words >> word) {
        std::string candidate = current.empty() ? word : current + " " + word;
        if (!current.empty() && width(candidate) > dest_rect.w) {
          lines.push_back(current);
          current = word;
        } else {
          current = candidate;
        }
      }
      if (!current.empty())
        lines.push_back(current);

      std::vector<std::string> colorValues = ini_reader->getList(name, "forecolor");
      SDL_Color color = {255, 228, 173, 255};
      if (colorValues.size() >= 3)
        color = {(uint8_t)std::atoi(colorValues[0].c_str()),
                 (uint8_t)std::atoi(colorValues[1].c_str()),
                 (uint8_t)std::atoi(colorValues[2].c_str()), 255};
      if (has_color_override)
        color = color_override;
      int lineHeight = resource_manager->getFontLineHeight(font, fontSize);
      int y = dest_rect.y;
      for (const std::string &line : lines) {
        SDL_Texture *t = resource_manager->getStringTexture(renderer, font, line,
                                                            color, fontSize);
        if (t) {
          int w = 0, h = 0;
          resource_manager->getTextSize(t, &w, &h);
          int x = dest_rect.x;
          if (justify == "center")
            x += (dest_rect.w - w) / 2;
          else if (justify == "right")
            x += dest_rect.w - w;
          SDL_Rect dst = {x, y, w, h};
          if (RenderSettings::drawsUiText())
            SDL_RenderCopy(renderer, t, NULL, &dst);
        }
        y += lineHeight;
      }
      return;
    }
  }

  // 2. Draw Background (only if multiline)
  if (is_multiline) {
      SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
      SDL_SetRenderDrawColor(renderer, 0, 0, 0, 60); // Dark background
      if (RenderSettings::drawsUiArt())
          SDL_RenderFillRect(renderer, &dest_rect);
      
      // Set Clipping
      SDL_RenderSetClipRect(renderer, &dest_rect);
  }

  // 3. Color Setup
  std::vector<std::string> color_values = ini_reader->getList(name, "forecolor");
  SDL_Color color = {255, 228, 173, 255}; 
  if (color_values.size() >= 3) {
      try { color = {(uint8_t)std::stoi(color_values[0]), (uint8_t)std::stoi(color_values[1]), (uint8_t)std::stoi(color_values[2]), 255}; } catch (...) {}
  }
  if (has_color_override)
      color = color_override;

  // 4. Render Lines
  int start_x = dest_rect.x + (is_multiline ? 4 : 0); 
  int start_y = dest_rect.y + (is_multiline ? 4 : 0); 
  int current = getScroll(this);
  int y_pos = start_y - current;
  
  for (const std::string& line : cached_lines) {
      // Optimization: Skip lines outside view
      if (is_multiline) {
          if (y_pos + 20 < dest_rect.y) { y_pos += 14; continue; }
          if (y_pos > dest_rect.y + dest_rect.h) break;
      }
      
      // If the line is empty (blank space), just skip drawing but ADVANCE Y
      if (line.empty()) {
          y_pos += 14; 
          continue; 
      }

      SDL_Texture* t = resource_manager->getStringTexture(
          renderer, font, line, color, ini_reader->getInt(name, "fontsize", 0));
      if (t) {
          int w, h;
          resource_manager->getTextSize(t, &w, &h);
          
          int draw_x = start_x;
          if (!is_multiline) {
              if (this->ini_reader->get(this->name, "justify") == "center") {
                  draw_x += (dest_rect.w - w) / 2;
              } else if (this->ini_reader->get(this->name, "justify") == "right") {
                  draw_x += (dest_rect.w - w);
              }
          }
          
          SDL_Rect dst = {draw_x, y_pos, w, h};
          if (RenderSettings::drawsUiText())
              SDL_RenderCopy(renderer, t, NULL, &dst);
      }
      y_pos += 14; 
  }

  // 5. Reset Clip
  if (is_multiline) SDL_RenderSetClipRect(renderer, NULL);

  // 6. Draw Scrollbar (Zoo Tycoon Style)
  int total_h = ((int)cached_lines.size() * 14) + 15;
  if (is_multiline && total_h > dest_rect.h && RenderSettings::drawsUiArt()) {
      int bar_x = dest_rect.x + dest_rect.w - 10;
      int bar_h = dest_rect.h;
      int bar_w = 8;
      
      // Track
      SDL_SetRenderDrawColor(renderer, 30, 30, 30, 100);
      SDL_Rect track = {bar_x, dest_rect.y, bar_w, bar_h};
      SDL_RenderFillRect(renderer, &track);
      
      float view_ratio = (float)dest_rect.h / total_h;
      float scroll_ratio = (float)current / (total_h - dest_rect.h);
      
      if (scroll_ratio > 1.0f) scroll_ratio = 1.0f;
      if (scroll_ratio < 0.0f) scroll_ratio = 0.0f;

      int thumb_h = (int)(dest_rect.h * view_ratio);
      if (thumb_h < 20) thumb_h = 20;
      
      int max_thumb_y = dest_rect.h - thumb_h;
      int thumb_y = dest_rect.y + (int)(scroll_ratio * max_thumb_y);
      
      // Gold Thumb
      SDL_SetRenderDrawColor(renderer, 181, 159, 109, 255); 
      SDL_Rect thumb = {bar_x + 1, thumb_y, bar_w - 2, thumb_h};
      SDL_RenderFillRect(renderer, &thumb);
  }
}