import os

def main():
    print("=== RESTORING PERFECT SCROLLABLE TEXT BOX ===")
    
    src_dir = os.path.join("src", "ui")
    os.makedirs(src_dir, exist_ok=True)
    
    # ---------------------------------------------------------
    # 1. WRITE UiText.hpp (The Definition)
    # ---------------------------------------------------------
    header_code = """#ifndef UI_TEXT_HPP
#define UI_TEXT_HPP

#include <string>
#include <vector>
#include <SDL2/SDL.h>
#include "UiElement.hpp"
#include "../IniReader.hpp"
#include "../ResourceManager.hpp"

class UiText : public UiElement {
public:
  UiText(IniReader * ini_reader, ResourceManager * resource_manager, std::string name);
  ~UiText();
  
  UiAction handleInputs(std::vector<Input> &inputs);
  void draw(SDL_Renderer * renderer, SDL_Rect * layout_rect);
  
  void setText(const std::string& newText);
  std::string getText() const { return text_string; }
  
private:
  std::string text_string = "";
  SDL_Texture * text = nullptr;
  SDL_Texture * shadow = nullptr;
  int font = 0;
  SDL_Rect dest_rect = {0, 0, 0, 0};
  
  // Cache for scrollable lines
  std::vector<std::string> cached_lines;
};

#endif // UI_TEXT_HPP
"""
    with open(os.path.join(src_dir, "UiText.hpp"), "w", encoding="utf-8") as f:
        f.write(header_code)
    print("✓ Restored UiText.hpp")

    # ---------------------------------------------------------
    # 2. WRITE UiText.cpp (The "Gold Standard" Implementation)
    # ---------------------------------------------------------
    # Features:
    # - Patch 09 Input Compatibility (SCROLL_UP/DOWN)
    # - Smart Backgrounds (Only for multiline)
    # - Gold Scrollbar (Zoo Tycoon Style)
    # - Bottom Padding Fix (+15px)
    
    cpp_code = """#include "UiText.hpp"
#include <sstream>
#include <vector>
#include <algorithm>
#include <cmath>
#include <map>
#include "../Input.hpp" 

// [STATE MANAGER]
static std::map<UiText*, int> scroll_y_map;

static int getScroll(UiText* ptr) { return scroll_y_map[ptr]; }
static void setScroll(UiText* ptr, int v) { scroll_y_map[ptr] = v; }

UiText::UiText(IniReader * ini_reader, ResourceManager * resource_manager, std::string name) {
  this->ini_reader = ini_reader;
  this->resource_manager = resource_manager;
  this->name = name;
  this->id = ini_reader->getInt(name, "id");
  this->font = ini_reader->getInt(name, "font");

  // Load initial text
  uint32_t string_id = (uint32_t) ini_reader->getUnsignedInt(name, "id");
  std::string raw = this->resource_manager->getString(string_id);
  if(raw.empty()) raw = (name == "version_label") ? "Version: ZT1-Engine 0.1" : "";
  
  this->setText(raw);
}

UiText::~UiText() {
  scroll_y_map.erase(this);
  if (text) SDL_DestroyTexture(text);
}

// [HELPER] Word Wrap
static std::vector<std::string> splitToLines(std::string text, size_t max_chars) {
    std::vector<std::string> lines;
    std::istringstream words(text);
    std::string word;
    std::string line;
    
    while (words >> word) {
        if (line.length() + word.length() + 1 > max_chars) {
            lines.push_back(line);
            line = word;
        } else {
            if (!line.empty()) line += " ";
            line += word;
        }
    }
    if (!line.empty()) lines.push_back(line);
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

// [INPUT] Mouse Wheel Support
UiAction UiText::handleInputs(std::vector<Input> &inputs) {
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
  if (this->cached_lines.empty()) return;

  // 1. Calculate Container
  dest_rect = this->getRect(this->ini_reader->getSection(this->name), layout_rect);
  
  bool is_multiline = (this->cached_lines.size() > 1);

  // 2. Draw Background (only if multiline)
  if (is_multiline) {
      SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
      SDL_SetRenderDrawColor(renderer, 0, 0, 0, 60); // Dark background
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

      SDL_Texture* t = resource_manager->getStringTexture(renderer, font, line, color);
      if (t) {
          int w, h;
          SDL_QueryTexture(t, NULL, NULL, &w, &h);
          
          int draw_x = start_x;
          if (!is_multiline) {
              if (this->ini_reader->get(this->name, "justify") == "center") {
                  draw_x += (dest_rect.w - w) / 2;
              } else if (this->ini_reader->get(this->name, "justify") == "right") {
                  draw_x += (dest_rect.w - w);
              }
          }
          
          SDL_Rect dst = {draw_x, y_pos, w, h};
          SDL_RenderCopy(renderer, t, NULL, &dst);
      }
      y_pos += 14; 
  }

  // 5. Reset Clip
  if (is_multiline) SDL_RenderSetClipRect(renderer, NULL);

  // 6. Draw Scrollbar (Zoo Tycoon Style)
  int total_h = ((int)cached_lines.size() * 14) + 15;
  if (is_multiline && total_h > dest_rect.h) {
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
"""
    with open(os.path.join(src_dir, "UiText.cpp"), "w", encoding="utf-8") as f:
        f.write(cpp_code)
    print("✓ Restored UiText.cpp (Gold Scrollbar + Padding + Crash Protection)")

if __name__ == "__main__":
    main()