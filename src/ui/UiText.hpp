#ifndef UI_TEXT_HPP
#define UI_TEXT_HPP

#include <functional>
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
  // A message for a box (dialogs): one paragraph, wrapped to the box's
  // width as it is drawn
  void setMessage(const std::string& newText);
  std::string getText() const { return text_string; }

  // UIEditableText (the zoo's name, its admission price): click to edit,
  // type, Enter (or a click elsewhere) keeps it, Esc puts it back.
  // numeric=1 takes digits and a point; charlimit= caps the length.
  bool isEditable() const { return editable; }
  // What the box holds while being edited (e.g. "22.00" for "$22.00"),
  // and what to do with the text kept
  std::function<std::string()> onBeginEdit;
  std::function<void(const std::string &)> onCommit;
  // Whether any box is being edited (ESC then cancels the edit instead of
  // opening the game menu)
  static bool isEditing();
  bool isBeingEdited() const; // (the game leaves its text alone meanwhile)
  void beginEdit();
  void endEdit(bool keep);
  
private:
  std::string text_string = "";
  bool has_color_override = false;
  SDL_Color color_override = {255, 255, 255, 255};
public:
  void setTextColor(SDL_Color c) { color_override = c; has_color_override = true; }
  // Back to the layout's forecolor
  void clearTextColor() { has_color_override = false; }
private:
  void drawLines(SDL_Renderer * renderer, SDL_Rect * layout_rect);
  SDL_Texture * text = nullptr;
  SDL_Texture * shadow = nullptr;
  int font = 0;
  SDL_Rect dest_rect = {0, 0, 0, 0};
  
  // Cache for scrollable lines
  std::vector<std::string> cached_lines;

  bool editable = false;
  bool numeric = false;
  int char_limit = 0;
  std::string before_edit;
};

#endif // UI_TEXT_HPP
