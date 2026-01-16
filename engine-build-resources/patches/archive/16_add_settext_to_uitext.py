import os

def apply(src_dir, root_dir):
    """Add setText() method to UiText for dynamic text updates"""
    
    # Update UiText.hpp
    hpp_path = os.path.join(src_dir, "ui", "UiText.hpp")
    if not os.path.exists(hpp_path):
        print("    ! UiText.hpp not found")
        return False
    
    with open(hpp_path, "r", encoding="utf-8") as f:
        content = f.read()
    
    if "setText" in content:
        return False  # Already patched
    
    # Add public inheritance and setText method
    new_hpp = '''#ifndef UI_TEXT_HPP
#define UI_TEXT_HPP
#include <string>
#include <SDL2/SDL.h>
#include "UiElement.hpp"
#include "../IniReader.hpp"
#include "../ResourceManager.hpp"

class UiText : public UiElement {  // [PATCH] Made public inheritance
public:
  UiText(IniReader * ini_reader, ResourceManager * resource_manager, std::string name);
  ~UiText();
  UiAction handleInputs(std::vector<Input> &inputs);
  void draw(SDL_Renderer * renderer, SDL_Rect * layout_rect);
  
  // [PATCH] Dynamic text update
  void setText(const std::string& newText);
  std::string getText() const { return text_string; }
  
private:
  std::string text_string = "";
  SDL_Texture * text = nullptr;
  SDL_Texture * shadow = nullptr;
  int font = 0;
  SDL_Rect dest_rect = {0, 0, 0, 0};
  SDL_Rect shadow_rect = {0, 0, 0, 0};
  bool text_dirty = false;  // [PATCH] Flag to regenerate texture
};
#endif // UI_TEXT_HPP
'''
    
    with open(hpp_path, "w", encoding="utf-8") as f:
        f.write(new_hpp)
    print("    -> Added setText() to UiText.hpp")
    
    # Update UiText.cpp
    cpp_path = os.path.join(src_dir, "ui", "UiText.cpp")
    if not os.path.exists(cpp_path):
        print("    ! UiText.cpp not found")
        return True
    
    with open(cpp_path, "r", encoding="utf-8") as f:
        cpp_content = f.read()
    
    if "UiText::setText" in cpp_content:
        return True  # Already has implementation
    
    new_cpp = '''#include "UiText.hpp"

UiText::UiText(IniReader * ini_reader, ResourceManager * resource_manager, std::string name) {
  this->ini_reader = ini_reader;
  this->resource_manager = resource_manager;
  this->name = name;
  this->id = ini_reader->getInt(name, "id");
  this->layer = ini_reader->getInt(name, "layer", 1);
  this->anchor = ini_reader->getInt(name, "anchor", 0);
  this->font = ini_reader->getInt(name, "font");

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
  if (text) SDL_DestroyTexture(text);
  if (shadow) SDL_DestroyTexture(shadow);
  for (UiElement * child : this->children) {
    free(child);
  }
}

// [PATCH] Set text dynamically
void UiText::setText(const std::string& newText) {
  if (text_string != newText) {
    text_string = newText;
    // Destroy old textures so they get regenerated
    if (text) {
      SDL_DestroyTexture(text);
      text = nullptr;
    }
    if (shadow) {
      SDL_DestroyTexture(shadow);
      shadow = nullptr;
    }
    text_dirty = true;
  }
}

UiAction UiText::handleInputs(std::vector<Input> &inputs) {
  return this->handleInputChildren(inputs);
}

void UiText::draw(SDL_Renderer * renderer, SDL_Rect * layout_rect) {
  if (!this->text_string.empty() && (!this->text || !this->shadow)) {
    std::vector<std::string> color_values = ini_reader->getList(name, "forecolor");
    SDL_Color color = {255, 228, 173, 255};  // Default color
    if (color_values.size() >= 3) {
      color = {
        (uint8_t) std::stoi(color_values[0]),
        (uint8_t) std::stoi(color_values[1]),
        (uint8_t) std::stoi(color_values[2]),
        255,
      };
    }
    this->text = this->resource_manager->getStringTexture(renderer, this->font, this->text_string, color);
    this->shadow = this->resource_manager->getStringTexture(renderer, this->font, this->text_string, {0, 0, 0, 255});
  }
  
  if (!this->text) return;  // [PATCH] Safety check
  
  dest_rect = this->getRect(this->ini_reader->getSection(this->name), layout_rect);
  SDL_QueryTexture(this->text, NULL, NULL, &dest_rect.w, &dest_rect.h);

  if (this->ini_reader->get(this->name, "justify") == "center") {
    dest_rect.x -= dest_rect.w / 2;
  } else if (this->ini_reader->get(this->name, "justify") == "right") {
    dest_rect.x -= dest_rect.w;
  }

  // Fix for version text on main menu
  if (this->ini_reader->get(this->name, "y") == "bottom") {
    dest_rect.y -= dest_rect.h;
  }

  shadow_rect = {dest_rect.x - 1, dest_rect.y + 1, dest_rect.w, dest_rect.h};
  if (this->shadow) SDL_RenderCopy(renderer, this->shadow, NULL, &shadow_rect);
  SDL_RenderCopy(renderer, this->text, NULL, &dest_rect);
  this->drawChildren(renderer, &dest_rect);
}
'''
    
    with open(cpp_path, "w", encoding="utf-8") as f:
        f.write(new_cpp)
    print("    -> Added setText() implementation to UiText.cpp")
    
    return True
