#ifndef UI_TEXT_HPP
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
  bool has_color_override = false;
  SDL_Color color_override = {255, 255, 255, 255};
public:
  void setTextColor(SDL_Color c) { color_override = c; has_color_override = true; }
private:
  SDL_Texture * text = nullptr;
  SDL_Texture * shadow = nullptr;
  int font = 0;
  SDL_Rect dest_rect = {0, 0, 0, 0};
  
  // Cache for scrollable lines
  std::vector<std::string> cached_lines;
};

#endif // UI_TEXT_HPP
