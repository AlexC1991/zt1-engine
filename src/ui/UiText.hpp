#ifndef UI_TEXT_HPP
#define UI_TEXT_HPP

#include <algorithm>
#include <functional>
#include <string>
#include <vector>
#include <SDL3/SDL.h>
#include "UiElement.hpp"
#include "UiScrollBar.hpp"
#include "../IniReader.hpp"
#include "../ResourceManager.hpp"

class UiText : public UiElement, public UiScrollable {
public:
  UiText(IniReader * ini_reader, ResourceManager * resource_manager, std::string name);
  ~UiText();
  
  UiAction handleInputs(std::vector<Input> &inputs);
  void draw(SDL_Renderer * renderer, SDL_Rect * layout_rect);
  
  void setText(const std::string& newText);
  // ours: a box the layout's own scroll bar (its scrollbar= element) scrolls,
  // as the lists' are: the text wrapped to the box's width into rows of the
  // font's line + 1 (a list's row), the bar along its right edge
  void linkScrollBar(UiScrollBar *bar) {
    bar->attach(this);
    this->linked = true;
  }
  bool scrollLinked() const { return this->linked; }
  // UiScrollable (rows)
  int getScrollPosition() const override { return this->rowScroll; }
  int getScrollMaximum() const override { return std::max(0, static_cast<int>(this->rows.size()) - this->viewRows); }
  int getScrollPage() const override { return this->viewRows; }
  void setScrollPosition(int position) override { this->rowScroll = std::clamp(position, 0, this->getScrollMaximum()); }
  SDL_Rect getScrollBounds() const override { return this->last_rect; }
  // A message for a box (dialogs): one paragraph, wrapped to the box's
  // width as it is drawn
  void setMessage(const std::string& newText);
  std::string getText() const { return text_string; }
  // ours: where its letters were last drawn (empty: none) - not its box
  SDL_Rect inkRect() const { return ink; }

  // UIEditableText (the zoo's name, its admission price): click to edit,
  // type, Enter (or a click elsewhere) keeps it, Esc puts it back.
  // numeric=1 takes digits and a point; charlimit= caps the length.
  bool isEditable() const { return editable; }
  // Drawn in a box of its own (the name dialog's field: a dark sunken box
  // with a frame, so it reads as a place to type)
  void setInputBox(bool on) { input_box = on; }
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
  // ours: the layout's forecolor (as drawn without an override)
  SDL_Color layoutColor() const;
private:
  void drawLines(SDL_Renderer * renderer, SDL_Rect * layout_rect);
  void drawRows(SDL_Renderer * renderer);
  bool linked = false;
  int rowScroll = 0, viewRows = 1, rowsWidth = -1;
  std::vector<std::string> rows;
  std::string rowsText;
  SDL_Texture * text = nullptr;
  SDL_Texture * shadow = nullptr;
  int font = 0;
  SDL_Rect dest_rect = {0, 0, 0, 0};
  SDL_Rect ink = {0, 0, 0, 0};
  void addInk(const SDL_Rect &r) {
    if (ink.w <= 0) ink = r;
    else SDL_GetRectUnion(&ink, &r, &ink);
  }
  
  // Cache for scrollable lines
  std::vector<std::string> cached_lines;

  bool editable = false;
  Uint32 edit_started = 0;
  bool held_at_start = false;
  bool input_box = false;
  bool numeric = false;
  int char_limit = 0;
  std::string before_edit;
};

#endif // UI_TEXT_HPP
