import os

def apply(src_dir, root_dir):
    """Add text wrapping support to UiText - safe version"""
    
    # Update UiText.hpp
    hpp_path = os.path.join(src_dir, "ui", "UiText.hpp")
    if not os.path.exists(hpp_path):
        print("    ! UiText.hpp not found")
        return False
    
    with open(hpp_path, "r", encoding="utf-8") as f:
        content = f.read()
    
    if "wrapText" in content:
        return False  # Already patched
    
    new_hpp = '''#ifndef UI_TEXT_HPP
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
  
  // Dynamic text update
  void setText(const std::string& newText);
  std::string getText() const { return text_string; }
  
private:
  std::string text_string = "";
  std::vector<SDL_Texture*> line_textures;   // Multiple lines for wrapped text
  std::vector<SDL_Texture*> shadow_textures; // Shadow for each line
  SDL_Texture * text = nullptr;
  SDL_Texture * shadow = nullptr;
  int font = 0;
  int max_width = 0;
  int max_height = 0;
  int line_height = 18;
  SDL_Rect dest_rect = {0, 0, 0, 0};
  SDL_Rect shadow_rect = {0, 0, 0, 0};
  bool text_dirty = false;
  bool use_wrapping = false;
  
  std::vector<std::string> wrapText(const std::string& text, int maxWidth);
  void clearTextures();
  void createWrappedTextures(SDL_Renderer* renderer, SDL_Color color);
};
#endif // UI_TEXT_HPP
'''
    
    with open(hpp_path, "w", encoding="utf-8") as f:
        f.write(new_hpp)
    print("    -> Added text wrapping to UiText.hpp")
    
    # Update UiText.cpp with safer implementation
    cpp_path = os.path.join(src_dir, "ui", "UiText.cpp")
    if not os.path.exists(cpp_path):
        print("    ! UiText.cpp not found")
        return True
    
    new_cpp = '''#include "UiText.hpp"
#include <sstream>

UiText::UiText(IniReader * ini_reader, ResourceManager * resource_manager, std::string name) {
  this->ini_reader = ini_reader;
  this->resource_manager = resource_manager;
  this->name = name;
  this->id = ini_reader->getInt(name, "id");
  this->layer = ini_reader->getInt(name, "layer", 1);
  this->anchor = ini_reader->getInt(name, "anchor", 0);
  this->font = ini_reader->getInt(name, "font");
  
  // Get max dimensions for text wrapping
  this->max_width = ini_reader->getInt(name, "dx", 0);
  this->max_height = ini_reader->getInt(name, "dy", 0);
  
  // Only enable wrapping for large text areas (like description boxes)
  // Must have both significant width AND height to be considered a text box
  this->use_wrapping = (max_width >= 200 && max_height >= 100);

  uint32_t string_id = (uint32_t) ini_reader->getUnsignedInt(name, "id");
  this->text_string = this->resource_manager->getString(string_id);
  if(this->text_string.empty()) {
    if (name == "version_label") {
      this->text_string = "Version Number: ZT1-Engine 0.1  ";
    } else {
      this->text_string = "Not found";
    }
  }
}

UiText::~UiText() {
  clearTextures();
  for (UiElement * child : this->children) {
    free(child);
  }
}

void UiText::clearTextures() {
  if (text) { SDL_DestroyTexture(text); text = nullptr; }
  if (shadow) { SDL_DestroyTexture(shadow); shadow = nullptr; }
  for (auto* tex : line_textures) {
    if (tex) SDL_DestroyTexture(tex);
  }
  line_textures.clear();
  for (auto* tex : shadow_textures) {
    if (tex) SDL_DestroyTexture(tex);
  }
  shadow_textures.clear();
}

void UiText::setText(const std::string& newText) {
  if (text_string != newText) {
    text_string = newText;
    clearTextures();
    text_dirty = true;
  }
}

std::vector<std::string> UiText::wrapText(const std::string& inputText, int maxWidth) {
  std::vector<std::string> lines;
  
  if (inputText.empty()) {
    return lines;
  }
  
  // Estimate characters per line (roughly 7 pixels per char)
  int charsPerLine = maxWidth / 7;
  if (charsPerLine < 10) charsPerLine = 10;
  
  std::istringstream stream(inputText);
  std::string paragraph;
  
  // Split by newlines first
  while (std::getline(stream, paragraph)) {
    if (paragraph.empty()) {
      lines.push_back("");
      continue;
    }
    
    // Word wrap within paragraph
    std::istringstream wordStream(paragraph);
    std::string word;
    std::string currentLine;
    
    while (wordStream >> word) {
      if (currentLine.empty()) {
        currentLine = word;
      } else if ((int)(currentLine.length() + 1 + word.length()) <= charsPerLine) {
        currentLine += " " + word;
      } else {
        lines.push_back(currentLine);
        currentLine = word;
      }
    }
    
    if (!currentLine.empty()) {
      lines.push_back(currentLine);
    }
  }
  
  return lines;
}

void UiText::createWrappedTextures(SDL_Renderer* renderer, SDL_Color color) {
  if (text_string.empty()) return;
  
  std::vector<std::string> lines = wrapText(text_string, max_width - 20);
  
  for (const std::string& line : lines) {
    if (line.empty()) {
      line_textures.push_back(nullptr);
      shadow_textures.push_back(nullptr);
    } else {
      SDL_Texture* lineTex = resource_manager->getStringTexture(renderer, font, line, color);
      SDL_Texture* shadowTex = resource_manager->getStringTexture(renderer, font, line, {0, 0, 0, 255});
      line_textures.push_back(lineTex);
      shadow_textures.push_back(shadowTex);
      
      // Get line height from first valid texture
      if (line_height == 18 && lineTex) {
        int h;
        SDL_QueryTexture(lineTex, NULL, NULL, NULL, &h);
        line_height = h + 3;
      }
    }
  }
}

UiAction UiText::handleInputs(std::vector<Input> &inputs) {
  return this->handleInputChildren(inputs);
}

void UiText::draw(SDL_Renderer * renderer, SDL_Rect * layout_rect) {
  // Get color
  std::vector<std::string> color_values = ini_reader->getList(name, "forecolor");
  SDL_Color color = {255, 228, 173, 255};
  if (color_values.size() >= 3) {
    try {
      color.r = (uint8_t) std::stoi(color_values[0]);
      color.g = (uint8_t) std::stoi(color_values[1]);
      color.b = (uint8_t) std::stoi(color_values[2]);
    } catch (...) {}
  }
  
  dest_rect = this->getRect(this->ini_reader->getSection(this->name), layout_rect);
  
  // Multi-line wrapped text rendering
  if (use_wrapping) {
    if (line_textures.empty() && !text_string.empty()) {
      createWrappedTextures(renderer, color);
    }
    
    int y_offset = 0;
    int maxLines = (max_height > 0) ? (max_height / line_height) : 50;
    
    for (size_t i = 0; i < line_textures.size() && (int)i < maxLines; i++) {
      if (line_textures[i]) {
        int tex_w, tex_h;
        SDL_QueryTexture(line_textures[i], NULL, NULL, &tex_w, &tex_h);
        
        SDL_Rect lineRect = {dest_rect.x, dest_rect.y + y_offset, tex_w, tex_h};
        SDL_Rect shadowRect = {lineRect.x - 1, lineRect.y + 1, tex_w, tex_h};
        
        if (shadow_textures[i]) {
          SDL_RenderCopy(renderer, shadow_textures[i], NULL, &shadowRect);
        }
        SDL_RenderCopy(renderer, line_textures[i], NULL, &lineRect);
      }
      y_offset += line_height;
    }
  } else {
    // Standard single-line rendering (unchanged from original)
    if (!this->text_string.empty() && (!this->text || !this->shadow)) {
      this->text = this->resource_manager->getStringTexture(renderer, this->font, this->text_string, color);
      this->shadow = this->resource_manager->getStringTexture(renderer, this->font, this->text_string, {0, 0, 0, 255});
    }
    
    if (!this->text) return;
    
    SDL_QueryTexture(this->text, NULL, NULL, &dest_rect.w, &dest_rect.h);

    if (this->ini_reader->get(this->name, "justify") == "center") {
      dest_rect.x -= dest_rect.w / 2;
    } else if (this->ini_reader->get(this->name, "justify") == "right") {
      dest_rect.x -= dest_rect.w;
    }

    if (this->ini_reader->get(this->name, "y") == "bottom") {
      dest_rect.y -= dest_rect.h;
    }

    shadow_rect = {dest_rect.x - 1, dest_rect.y + 1, dest_rect.w, dest_rect.h};
    if (this->shadow) SDL_RenderCopy(renderer, this->shadow, NULL, &shadow_rect);
    SDL_RenderCopy(renderer, this->text, NULL, &dest_rect);
  }
  
  this->drawChildren(renderer, &dest_rect);
}
'''
    
    with open(cpp_path, "w", encoding="utf-8") as f:
        f.write(new_cpp)
    print("    -> Added safe text wrapping to UiText.cpp")
    
    return True
